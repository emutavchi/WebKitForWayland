#include <WebKit/WKContext.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKPageGroup.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKRetainPtr.h>
#include <WebKit/WKString.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKView.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <glib.h>
#include <WebKit/WKUserMediaPermissionRequest.h>
#include <WebKit/WKUserMediaPermissionCheck.h>
#include <WebKit/WKArray.h>
#include <string>

static WKViewRef createView(WKPageConfigurationRef);

std::string toStdString(WKStringRef string)
{
    size_t size = WKStringGetMaximumUTF8CStringSize(string);
    auto buffer = std::make_unique<char[]>(size);
    size_t len = WKStringGetUTF8CString(string, buffer.get(), size);

    return std::string(buffer.get(), len - 1);
}

void decidePolicyForUserMediaPermissionRequestCallBack(WKPageRef, WKFrameRef, WKSecurityOriginRef, WKSecurityOriginRef, WKUserMediaPermissionRequestRef permissionRequest, const void* /* clientInfo */)
{
    WKRetainPtr<WKArrayRef> audioDeviceUIDs = WKUserMediaPermissionRequestAudioDeviceUIDs(permissionRequest);
    WKRetainPtr<WKArrayRef> videoDeviceUIDs = WKUserMediaPermissionRequestVideoDeviceUIDs(permissionRequest);

    if (WKArrayGetSize(videoDeviceUIDs.get()) || WKArrayGetSize(audioDeviceUIDs.get())) {
        WKRetainPtr<WKStringRef> videoDeviceUID;
        if (WKArrayGetSize(videoDeviceUIDs.get()))
            videoDeviceUID = reinterpret_cast<WKStringRef>(WKArrayGetItemAtIndex(videoDeviceUIDs.get(), 0));
        else
            videoDeviceUID = WKStringCreateWithUTF8CString("");

        WKRetainPtr<WKStringRef> audioDeviceUID;
        if (WKArrayGetSize(audioDeviceUIDs.get()))
            audioDeviceUID = reinterpret_cast<WKStringRef>(WKArrayGetItemAtIndex(audioDeviceUIDs.get(), 0));
        else
            audioDeviceUID = WKStringCreateWithUTF8CString("");

        printf("Accept: audio='%s' video='%s'\n", toStdString(audioDeviceUID.get()).c_str(), toStdString(videoDeviceUID.get()).c_str());

        WKUserMediaPermissionRequestAllow(permissionRequest, audioDeviceUID.get(), videoDeviceUID.get());
    }
}

void checkUserMediaPermissionForOrigin(WKPageRef page, WKFrameRef frame,
                                       WKSecurityOriginRef userMediaDocumentOrigin,
                                       WKSecurityOriginRef topLevelDocumentOrigin,
                                       WKUserMediaPermissionCheckRef devicesRequest, const void *)
{
    WKUserMediaPermissionCheckSetUserMediaAccessInfo(
        devicesRequest,
        WKStringCreateWithUTF8CString("test-test"),
        true);
}

static WKPageUIClientV6 createPageUIClient()
{
    WKPageUIClientV6 pageUIClient = {
        { 6, nullptr },
        nullptr, // createNewPage_deprecatedForUseWithV0
        nullptr, // showPage
        nullptr, // close
        nullptr, // takeFocus
        nullptr, // focus
        nullptr, // unfocus
        nullptr, // runJavaScriptAlert_deprecatedForUseWithV0
        nullptr, // runJavaScriptConfirm_deprecatedForUseWithV0
        nullptr, // runJavaScriptPrompt_deprecatedForUseWithV0
        nullptr, // setStatusText
        nullptr, // mouseDidMoveOverElement_deprecatedForUseWithV0
        nullptr, // missingPluginButtonClicked_deprecatedForUseWithV0
        nullptr, // didNotHandleKeyEvent
        nullptr, // didNotHandleWheelEvent
        nullptr, // toolbarsAreVisible
        nullptr, // setToolbarsAreVisible
        nullptr, // menuBarIsVisible
        nullptr, // setMenuBarIsVisible
        nullptr, // statusBarIsVisible
        nullptr, // setStatusBarIsVisible
        nullptr, // isResizable
        nullptr, // setIsResizable
        nullptr, // getWindowFrame
        nullptr, // setWindowFrame
        nullptr, // runBeforeUnloadConfirmPanel
        nullptr, // didDraw
        nullptr, // pageDidScroll
        nullptr, // exceededDatabaseQuota
        nullptr, // runOpenPanel
        nullptr, // decidePolicyForGeolocationPermissionRequest
        nullptr, // headerHeight
        nullptr, // footerHeight
        nullptr, // drawHeader
        nullptr, // drawFooter
        nullptr, // printFrame
        nullptr, // runModal
        nullptr, // unused1
        nullptr, // saveDataToFileInDownloadsFolder
        nullptr, // shouldInterruptJavaScript_unavailable
        nullptr, // createNewPage_deprecatedForUseWithV1
        nullptr, // mouseDidMoveOverElement
        nullptr, // decidePolicyForNotificationPermissionRequest
        nullptr, // unavailablePluginButtonClicked_deprecatedForUseWithV1
        nullptr, // showColorPicker
        nullptr, // hideColorPicker
        nullptr, // unavailablePluginButtonClicked
        nullptr, // pinnedStateDidChange
        nullptr, // didBeginTrackingPotentialLongMousePress
        nullptr, // didRecognizeLongMousePress
        nullptr, // didCancelTrackingPotentialLongMousePress
        nullptr, // isPlayingAudioDidChange
        decidePolicyForUserMediaPermissionRequestCallBack, // decidePolicyForUserMediaPermissionRequest
        nullptr, // didClickAutoFillButton
        nullptr, // runJavaScriptAlert
        nullptr, // runJavaScriptConfirm
        nullptr, // runJavaScriptPrompt
        nullptr, // mediaSessionMetadataDidChange
        // createNewPage
        [](WKPageRef, WKPageConfigurationRef pageConfiguration, WKNavigationActionRef, WKWindowFeaturesRef, const void*) -> WKPageRef {
            auto view = createView(pageConfiguration);
            auto page = WKViewGetPage(view);
            WKRetain(page);
            return page;
        },
        0, // runJavaScriptAlert
        0, // runJavaScriptConfirm
        0, // runJavaScriptPrompt
        checkUserMediaPermissionForOrigin,
    };
    return pageUIClient;
}

static WKPageNavigationClientV0 createPageNavigationClient()
{
    WKPageNavigationClientV0 navigationClient = {
        { 0, nullptr },
        // decidePolicyForNavigationAction
        [](WKPageRef, WKNavigationActionRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
            WKFramePolicyListenerUse(listener);
        },
        // decidePolicyForNavigationResponse
        [](WKPageRef, WKNavigationResponseRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
            WKFramePolicyListenerUse(listener);
        },
        nullptr, // decidePolicyForPluginLoad
        nullptr, // didStartProvisionalNavigation
        nullptr, // didReceiveServerRedirectForProvisionalNavigation
        nullptr, // didFailProvisionalNavigation
        nullptr, // didCommitNavigation
        nullptr, // didFinishNavigation
        nullptr, // didFailNavigation
        nullptr, // didFailProvisionalLoadInSubframe
        nullptr, // didFinishDocumentLoad
        nullptr, // didSameDocumentNavigation
        nullptr, // renderingProgressDidChange
        nullptr, // canAuthenticateAgainstProtectionSpace
        nullptr, // didReceiveAuthenticationChallenge
        nullptr, // webProcessDidCrash
        nullptr, // copyWebCryptoMasterKey
        nullptr, // didBeginNavigationGesture
        nullptr, // willEndNavigationGesture
        nullptr, // didEndNavigationGesture
        nullptr, // didRemoveNavigationGestureSnapshot
    };
    return navigationClient;
}

static WKViewRef createView(WKPageConfigurationRef pageConfiguration)
{
    auto view = WKViewCreate(pageConfiguration);
    auto page = WKViewGetPage(view);

    auto pageUIClient = createPageUIClient();
    WKPageSetPageUIClient(page, &pageUIClient.base);

    auto pageNavigationClient = createPageNavigationClient();
    WKPageSetPageNavigationClient(page, &pageNavigationClient.base);

    return view;
}

int main(int argc, char* argv[])
{
    GMainLoop* loop = g_main_loop_new(g_main_context_default(), FALSE);

    auto context = adoptWK(WKContextCreate());
    auto pageGroupIdentifier = adoptWK(WKStringCreateWithUTF8CString("WPEPageGroup"));
    auto pageGroup = adoptWK(WKPageGroupCreateWithIdentifier(pageGroupIdentifier.get()));
    auto pageConfiguration = adoptWK(WKPageConfigurationCreate());
    WKPageConfigurationSetContext(pageConfiguration.get(), context.get());
    WKPageConfigurationSetPageGroup(pageConfiguration.get(), pageGroup.get());

    auto preferences = adoptWK(WKPreferencesCreate());
    WKPageGroupSetPreferences(pageGroup.get(), preferences.get());
    WKPageConfigurationSetPreferences(pageConfiguration.get(), preferences.get());
    WKPreferencesSetAllowRunningOfInsecureContent(preferences.get(), true);
    WKPreferencesSetAllowDisplayOfInsecureContent(preferences.get(), true);
    WKPreferencesSetLogsPageMessagesToSystemConsoleEnabled(preferences.get(), true);

    auto view = adoptWK(createView(pageConfiguration.get()));

    const char* url = "http://www.webkit.org/blog-files/3d-transforms/poster-circle.html";
    if (argc > 1)
        url = argv[1];

    auto shellURL = adoptWK(WKURLCreateWithUTF8CString(url));
    WKPageLoadURL(WKViewGetPage(view.get()), shellURL.get());

    WKPreferencesSetTabToLinksEnabled(preferences.get(), true);
    WKPreferencesSetSpatialNavigationEnabled(preferences.get(), true);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    return 0;
}
