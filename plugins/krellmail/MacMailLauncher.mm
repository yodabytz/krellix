#import <AppKit/AppKit.h>

#include <QLoggingCategory>

bool launchMacDefaultMail()
{
    @autoreleasepool {
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        // Resolve the mailto association without sending a URL to the app.
        NSURL *app = [workspace URLForApplicationToOpenURL:[NSURL URLWithString:@"mailto:"]];
        if (!app) return false;
        [workspace openApplicationAtURL:app
                          configuration:[NSWorkspaceOpenConfiguration configuration]
                      completionHandler:^(NSRunningApplication *, NSError *error) {
            if (error) qWarning("KrellMail: could not open the default mail app");
        }];
        return true;
    }
}
