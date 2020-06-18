/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"

#if ENABLE(GAMEPAD)

#include <wtf/NeverDestroyed.h>

#include "WPEGamepadProvider.h"
#include "Logging.h"
#include "GamepadProviderClient.h"

#include <wpe/wpe.h>

namespace WebCore {

static const Seconds connectionDelayInterval { 50_ms };
static const Seconds inputNotificationDelay { 16_ms };

WPEGamepadProvider& WPEGamepadProvider::singleton()
{
    static NeverDestroyed<WPEGamepadProvider> sharedProvider;
    return sharedProvider;
}

WPEGamepadProvider::WPEGamepadProvider()
    : m_connectionDelayTimer(*this, &WPEGamepadProvider::connectionDelayTimerFired)
    , m_inputNotificationTimer(*this, &WPEGamepadProvider::inputNotificationTimerFired)
{
}

void WPEGamepadProvider::startMonitoringGamepads(GamepadProviderClient& client)
{
    bool isFirstClient = m_clients.isEmpty();

    ASSERT(!m_clients.contains(&client));
    m_clients.add(&client);

    LOG(Gamepad, "WPEGamepadProvider::startMonitoringGamepads isFirstClient=%s", isFirstClient ? "yes" : "no");

    if (isFirstClient && !m_provider)  {
        m_shouldDispatchCallbacks = false;
        m_provider = wpe_gamepad_provider_create();
        if (m_provider) {
            static struct wpe_gamepad_provider_client s_client = {
                // gamepad_connected
                [](void* data, uint32_t gamepad_id) {
                    auto& provider = *reinterpret_cast<WPEGamepadProvider*>(data);
                    provider.gamepadConnected(gamepad_id);
                },
                // gamepad_disconnected
                [](void* data, uint32_t gamepad_id) {
                    auto& provider = *reinterpret_cast<WPEGamepadProvider*>(data);
                    provider.gamepadDiconnected(gamepad_id);
                }
            };
            wpe_gamepad_provider_set_client(m_provider, &s_client, this);
            wpe_gamepad_provider_start(m_provider);
            m_connectionDelayTimer.startOneShot(connectionDelayInterval);
        }
    }
}

void WPEGamepadProvider::stopMonitoringGamepads(GamepadProviderClient& client)
{
    bool isLastClient = m_clients.remove(&client) && m_clients.isEmpty();
    LOG(Gamepad, "WPEGamepadProvider::stopMonitoringGamepads isLastClient=%s", isLastClient ? "yes" : "no");

    if (isLastClient) {
        m_gamepadVector.clear();
        m_gamepadMap.clear();
        if (m_provider) {
            wpe_gamepad_provider_set_client(m_provider, nullptr, nullptr);
            wpe_gamepad_provider_stop(m_provider);
            wpe_gamepad_provider_destroy(m_provider);
            m_provider = nullptr;
            m_lastActiveGamepad = nullptr;
        }
    }
}

void WPEGamepadProvider::gamepadConnected(uint32_t id)
{
    if (m_gamepadMap.contains(id))
        return;

    unsigned index = 0;
    while (index < m_gamepadVector.size() && m_gamepadVector[index])
        ++index;

    if (m_gamepadVector.size() <= index)
        m_gamepadVector.grow(index + 1);

    auto gamepad = std::make_unique<WPEGamepad>(m_provider, id, index);
    m_gamepadVector[index] = gamepad.get();
    m_gamepadMap.set(id, WTFMove(gamepad));

    if (!m_shouldDispatchCallbacks) {
        m_connectionDelayTimer.startOneShot(0_s);
        return;
    }

    for (auto& client : m_clients)
        client->platformGamepadConnected(*m_gamepadVector[index]);
}

void WPEGamepadProvider::gamepadDiconnected(uint32_t id)
{
    if (!m_gamepadMap.contains(id))
        return;

    if (m_lastActiveGamepad && id == wpe_gamepad_get_id(m_lastActiveGamepad))
        m_lastActiveGamepad = nullptr;

    auto gamepad = m_gamepadMap.take(id);
    auto index = m_gamepadVector.find(gamepad.get());
    if (index != notFound)
        m_gamepadVector[index] = nullptr;

    m_shouldDispatchCallbacks = true;

    for (auto& client : m_clients)
        client->platformGamepadDisconnected(*gamepad);
}

void WPEGamepadProvider::connectionDelayTimerFired()
{
    m_shouldDispatchCallbacks = true;

    for (auto* client : m_clients)
        client->setInitialConnectedGamepads(m_gamepadVector);
}

void WPEGamepadProvider::inputNotificationTimerFired()
{
    if (!m_shouldDispatchCallbacks)
        return;
    dispatchPlatformGamepadInputActivity();
}

void WPEGamepadProvider::platformStopMonitoringInput()
{
    if (m_provider)
        wpe_gamepad_provider_stop(m_provider);
}

void WPEGamepadProvider::platformStartMonitoringInput()
{
    if (m_provider)
        wpe_gamepad_provider_start(m_provider);
}

void WPEGamepadProvider::scheduleInputNotification(struct wpe_gamepad* gamepad, bool shouldMakeGamepadsVisibile /* = false */)
{
    m_lastActiveGamepad = gamepad;
    if (shouldMakeGamepadsVisibile)
        setShouldMakeGamepadsVisibile();
    if (!m_inputNotificationTimer.isActive())
        m_inputNotificationTimer.startOneShot(inputNotificationDelay);
}

struct wpe_view_backend* WPEGamepadProvider::viewForGamepadInput()
{
    if (m_provider)
        return wpe_gamepad_provider_get_view_for_gamepad_input(m_provider, m_lastActiveGamepad);
    return nullptr;
}

} // namespace WebCore

#endif // ENABLE(GAMEPAD)
