#include <assert.h>
#include <string.h>
#include "ble_devices.h"

int main(void)
{
    ble_devices_t list={0};
    ble_device_t first={.type=0,.address={1,2,3,4,5,6},.rssi=-75};
    ble_devices_add(&list,&first); /* Connectable advertisement arrives before its name. */
    strcpy(first.name,"OBD BLE"); first.rssi=-45;
    ble_devices_add(&list,&first);
    assert(list.count==1 && !strcmp(list.items[0].name,"OBD BLE"));
    uint32_t named_revision=list.revision;
    first.name[0]=0; first.rssi=-60;
    ble_devices_add(&list,&first);
    assert(list.revision==named_revision && !strcmp(list.items[0].name,"OBD BLE"));
    assert(list.items[0].rssi==-60);
    ble_device_t second=first;
    strcpy(second.name,"OBD BLE"); second.type=1;
    ble_devices_add(&list,&second);
    assert(list.count==2 && !ble_device_same(&list.items[0],&list.items[1]));
    second.address[0]=2;
    ble_devices_add(&list,&second);
    assert(list.count==3); /* Same name is not an identity. Address type is part of it. */
    char address[18]; ble_device_address(&list.items[0],address);
    assert(!strcmp(address,"06:05:04:03:02:01"));
    ble_device_t selected=list.items[2];
    for (unsigned i=3;i<BLE_DEVICE_LIMIT+5;++i) {
        second.address[0]=i;
        ble_devices_add(&list,&second);
    }
    assert(list.count==BLE_DEVICE_LIMIT && ble_device_same(&selected,&list.items[2]));
    first.name[0]='X'; first.name[1]=0;
    ble_devices_add(&list,&first); /* Full list still updates existing devices. */
    assert(!strcmp(list.items[0].name,"X"));
    ble_reconnect_policy_t policy={0};
    assert(ble_reconnect_startup(&policy,true,true));
    assert(!ble_reconnect_startup(&policy,true,true)); /* Host resync cannot restart a failed boot attempt. */
    assert(!ble_reconnect_take(&policy,true,true,false)); /* A failed connection did not validate an adapter. */
    policy.allowed=true; /* Successful ELM initialization arms a future unexpected disconnect. */
    assert(ble_reconnect_take(&policy,true,true,false));
    assert(!ble_reconnect_take(&policy,true,true,false));
    policy.allowed=true;
    assert(!ble_reconnect_take(&policy,true,true,true)); /* User disconnect suppresses and consumes it. */
    assert(!ble_reconnect_take(&policy,true,true,false));
    policy.allowed=true;
    assert(!ble_reconnect_take(&policy,false,true,false));
    policy.allowed=true;
    assert(!ble_reconnect_take(&policy,true,false,false)); /* Forgotten devices cannot reconnect. */
    policy=(ble_reconnect_policy_t){0};
    assert(!ble_reconnect_startup(&policy,true,false));
    return 0;
}
