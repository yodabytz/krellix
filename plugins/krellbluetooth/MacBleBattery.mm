#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

// Read only the standard Battery Service of devices already connected to macOS.
// A CoreBluetooth connection here is this process's reference to that connection;
// releasing it does not disconnect another application's/system's reference.
@interface BatteryReader : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property(nonatomic, strong) CBCentralManager *manager;
@property(nonatomic, strong) NSArray<CBPeripheral *> *devices;
@property(nonatomic, strong) NSMutableArray *readings;
@property(nonatomic, strong) NSMutableSet *pending;
@property(nonatomic) BOOL done;
@end
@implementation BatteryReader
- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    if (central.state == CBManagerStateUnknown || central.state == CBManagerStateResetting) return;
    if (central.state != CBManagerStatePoweredOn) { self.done = YES; return; }
    self.devices = [central retrieveConnectedPeripheralsWithServices:@[[CBUUID UUIDWithString:@"180F"], [CBUUID UUIDWithString:@"1812"]]];
    for (CBPeripheral *device in self.devices) [self.pending addObject:device.identifier];
    if (!self.pending.count) self.done = YES;
    for (CBPeripheral *device in self.devices) {
        device.delegate = self;
        [central connectPeripheral:device options:nil];
    }
}
- (void)finish:(CBPeripheral *)device {
    [self.pending removeObject:device.identifier];
    if (!self.pending.count) self.done = YES;
}
- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)device {
    (void)central;
    [device discoverServices:@[[CBUUID UUIDWithString:@"180F"]]];
}
- (void)centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)device error:(NSError *)error {
    (void)central; (void)error;
    [self finish:device];
}
- (void)peripheral:(CBPeripheral *)device didDiscoverServices:(NSError *)error {
    if (error || !device.services.count) { [self finish:device]; return; }
    for (CBService *service in device.services)
        if ([service.UUID isEqual:[CBUUID UUIDWithString:@"180F"]]) {
            [device discoverCharacteristics:@[[CBUUID UUIDWithString:@"2A19"]] forService:service];
            return;
        }
    [self finish:device];
}
- (void)peripheral:(CBPeripheral *)device didDiscoverCharacteristicsForService:(CBService *)service error:(NSError *)error {
    if (!error) for (CBCharacteristic *value in service.characteristics)
        if ([value.UUID isEqual:[CBUUID UUIDWithString:@"2A19"]] && (value.properties & CBCharacteristicPropertyRead)) {
            [device readValueForCharacteristic:value];
            return;
        }
    [self finish:device];
}
- (void)peripheral:(CBPeripheral *)device didUpdateValueForCharacteristic:(CBCharacteristic *)value error:(NSError *)error {
    if (!error && [value.UUID isEqual:[CBUUID UUIDWithString:@"2A19"]] && value.value.length == 1 && device.name.length) {
        const uint8_t percent = *static_cast<const uint8_t *>(value.value.bytes);
        if (percent <= 100) [self.readings addObject:@{@"name":device.name, @"id":device.identifier.UUIDString, @"battery":@(percent), @"source":@"ble"}];
    }
    [self finish:device];
}
@end

static NSArray *accessoryReadings() {
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/pmset"];
    task.arguments = @[@"-g", @"accps", @"-xml"];
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![task launchAndReturnError:nil]) return @[];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2];
    while (task.running && deadline.timeIntervalSinceNow > 0) [NSThread sleepForTimeInterval:0.02];
    if (task.running) { [task terminate]; return @[]; }
    if (task.terminationStatus != 0) return @[];
    NSString *xml = [[NSString alloc] initWithData:[pipe.fileHandleForReading readDataToEndOfFile] encoding:NSUTF8StringEncoding];
    NSMutableArray *readings = [NSMutableArray new];
    NSArray *parts = [xml componentsSeparatedByString:@"<?xml"];
    for (NSUInteger index = 1; index < parts.count; ++index) {
        NSData *data = [[@"<?xml" stringByAppendingString:parts[index]] dataUsingEncoding:NSUTF8StringEncoding];
        id value = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
        if (![value isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary *entry = value;
        NSString *transport = entry[@"Transport Type"], *address = entry[@"Accessory Identifier"], *name = entry[@"Name"];
        NSNumber *capacity = entry[@"Current Capacity"], *maximum = entry[@"Max Capacity"];
        if (![transport isKindOfClass:[NSString class]] || ![transport hasPrefix:@"Bluetooth"]
            || ![address isKindOfClass:[NSString class]] || !address.length
            || ![capacity isKindOfClass:[NSNumber class]] || ![maximum isKindOfClass:[NSNumber class]]
            || maximum.doubleValue <= 0 || capacity.doubleValue < 0 || capacity.doubleValue > maximum.doubleValue
            || (entry[@"Is Present"] && ![entry[@"Is Present"] boolValue])) continue;
        const int percentage = (int)(100.0 * capacity.doubleValue / maximum.doubleValue);
        [readings addObject:@{@"name":[name isKindOfClass:[NSString class]] ? name : @"",
            @"address":address, @"battery":@(percentage), @"source":@"accessory"}];
    }
    return readings;
}
int main() {
    @autoreleasepool {
        BatteryReader *reader = [BatteryReader new];
        reader.pending = [NSMutableSet new];
        reader.readings = [accessoryReadings() mutableCopy];
        reader.manager = [[CBCentralManager alloc] initWithDelegate:reader queue:nil options:@{CBCentralManagerOptionShowPowerAlertKey:@NO}];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:6];
        while (!reader.done && deadline.timeIntervalSinceNow > 0)
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        for (CBPeripheral *device in reader.devices) {
            device.delegate = nil;
            [reader.manager cancelPeripheralConnection:device];
        }
        reader.manager.delegate = nil;
        NSData *json = [NSJSONSerialization dataWithJSONObject:reader.readings options:0 error:nil];
        if (json) fwrite(json.bytes, 1, json.length, stdout);
        return json ? 0 : 1;
    }
}
