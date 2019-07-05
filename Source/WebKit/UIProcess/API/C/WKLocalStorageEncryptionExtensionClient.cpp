/*
* Copyright (c) 2018, Comcast
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
*  * Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR OR; PROFITS BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY OF THEORY LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"

#include "APIClient.h"
#include "APIData.h"
#include "APILocalStorageEncryptionExtensionClient.h"
#include "LocalStorageEncryptionExtension.h"
#include "WKAPICast.h"
#include "WKLocalStorageEncryptionExtensionClient.h"

#include <WebCore/SecurityOriginData.h>

#include <cstring>

using namespace WebKit;

namespace API {
template<> struct ClientTraits<WKLocalStorageEncryptionExtensionClientBase> {
    typedef std::tuple<WKLocalStorageEncryptionExtensionClientV0> Versions;
};
}

void WKLocalStorageEncryptionExtensionSetClient(const WKLocalStorageEncryptionExtensionClientBase* wkClient)
{
    if (!wkClient) {
        LocalStorageEncryptionExtension::singleton().setClient(nullptr);
        return;
    }

    class WebLocalStorageEncryptionExtensionClient : public API::Client<WKLocalStorageEncryptionExtensionClientBase>, public API::LocalStorageEncryptionExtensionClient {
    public:
        explicit WebLocalStorageEncryptionExtensionClient(const WKLocalStorageEncryptionExtensionClientBase* client) {
            initialize(client);
        }
    private:
        std::optional<Vector<uint8_t>> loadKeyWithOrigin(const WebCore::SecurityOriginData& securityOriginData) final {
            if (!m_client.loadKeyWithOrigin)
                return std::nullopt;
 
            WKDataRef keyDataRef = nullptr;
            RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(securityOriginData.protocol, securityOriginData.host, securityOriginData.port);
            m_client.loadKeyWithOrigin(toAPI(securityOrigin.get()), &keyDataRef, m_client.base.clientInfo);
 
            if (!keyDataRef)
                return std::nullopt;
 
            auto data = adoptRef(WebKit::toImpl(keyDataRef));
            Vector<uint8_t> keyVector;
            keyVector.append(data->bytes(), data->size());
            ::memset(const_cast<unsigned char*>(data->bytes()), 0, data->size());
            return std::make_optional(WTFMove(keyVector));
        }
    };
 
    auto client = std::make_unique<WebLocalStorageEncryptionExtensionClient>(wkClient);
    LocalStorageEncryptionExtension::singleton().setClient(WTFMove(client));
}

