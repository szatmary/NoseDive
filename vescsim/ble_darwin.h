#ifndef BLE_DARWIN_H
#define BLE_DARWIN_H

// C API — implemented in ObjC, called from Go.
void bleInit(const char *name, const char *serviceUUID, const char *rxUUID, const char *txUUID);
int bleSendNotification(const void *data, int len);
int bleIsReady(void);
int bleGetMTU(void);

#endif
