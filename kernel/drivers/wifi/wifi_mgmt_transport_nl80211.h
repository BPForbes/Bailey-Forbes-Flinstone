#ifndef WIFI_MGMT_TRANSPORT_NL80211_H
#define WIFI_MGMT_TRANSPORT_NL80211_H

#include "wifi_mgmt_transport.h"

typedef struct fl_net_wifi_nl80211 fl_net_wifi_nl80211_t;

int wifi_mgmt_transport_nl80211_init(wifi_mgmt_transport_t *tr, fl_net_wifi_nl80211_t *nl);

#endif /* WIFI_MGMT_TRANSPORT_NL80211_H */
