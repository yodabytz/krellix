#import <AppKit/AppKit.h>
bool configureMacAccessoryApplication() {
    NSApplication *app = [NSApplication sharedApplication];
    return [app activationPolicy] == NSApplicationActivationPolicyAccessory
        || [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
}
bool isMacAccessoryApplication() {
    return [[NSApplication sharedApplication] activationPolicy] == NSApplicationActivationPolicyAccessory;
}
