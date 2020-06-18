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
#pragma once

#if ENABLE(GAMEPAD)

#include "PlatformGamepad.h"

struct wpe_gamepad;
struct wpe_gamepad_provider;

namespace WebCore {

class WPEGamepad : public PlatformGamepad {
public:
    WPEGamepad(struct wpe_gamepad_provider* provider, uint32_t gamepad_id, unsigned index);
    virtual ~WPEGamepad();

    const Vector<double>& axisValues() const final { return m_axisValues; }
    const Vector<double>& buttonValues() const final { return m_buttonValues; }

private:
    void updateButtonValues(struct wpe_gamepad*);
    void updateAxisValues(struct wpe_gamepad*);

    Vector<double> m_buttonValues;
    Vector<double> m_axisValues;

    struct wpe_gamepad* m_gamepad { nullptr };
};

} // namespace WebCore

#endif // ENABLE(GAMEPAD)
