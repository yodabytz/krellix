#include "PlaybackPermission.h"

#import <AVFoundation/AVFoundation.h>

PlaybackPermission macPlaybackPermission(bool request)
{
    const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusAuthorized) return PlaybackPermission::Allowed;
    if (status != AVAuthorizationStatusNotDetermined) return PlaybackPermission::Denied;
    if (request) [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {}];
    return PlaybackPermission::Waiting;
}
