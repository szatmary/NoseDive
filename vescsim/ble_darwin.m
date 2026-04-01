#import <CoreBluetooth/CoreBluetooth.h>
#import <dispatch/dispatch.h>
#import "ble_darwin.h"
#import "_cgo_export.h"

@interface VESCPeripheral : NSObject <CBPeripheralManagerDelegate>
@property (strong) CBPeripheralManager *manager;
@property (strong) CBMutableCharacteristic *txChar;
@property (strong) CBMutableCharacteristic *rxChar;
@property (strong) CBCentral *subscribedCentral;
@property (copy) NSString *localName;
@property (copy) CBUUID *serviceUUID;
@property (copy) CBUUID *rxUUID;
@property (copy) CBUUID *txUUID;
@property BOOL ready;
@property NSUInteger negotiatedMTU;
@property (strong) NSMutableArray<NSData *> *txQueue;
@end

@implementation VESCPeripheral

- (instancetype)init {
    self = [super init];
    if (self) {
        _txQueue = [NSMutableArray new];
        _negotiatedMTU = 20; // BLE 4.0 default
    }
    return self;
}

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral {
    goBLEStateUpdate((int)peripheral.state);
    if (peripheral.state == CBManagerStatePoweredOn) {
        self.ready = YES;

        self.rxChar = [[CBMutableCharacteristic alloc]
            initWithType:self.rxUUID
            properties:CBCharacteristicPropertyWrite | CBCharacteristicPropertyWriteWithoutResponse
            value:nil
            permissions:CBAttributePermissionsWriteable];

        self.txChar = [[CBMutableCharacteristic alloc]
            initWithType:self.txUUID
            properties:CBCharacteristicPropertyNotify | CBCharacteristicPropertyRead
            value:nil
            permissions:CBAttributePermissionsReadable];

        CBMutableService *svc = [[CBMutableService alloc] initWithType:self.serviceUUID primary:YES];
        svc.characteristics = @[self.rxChar, self.txChar];
        [peripheral addService:svc];
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral didAddService:(CBService *)service error:(NSError *)error {
    if (error) {
        NSLog(@"BLE: addService error: %@", error);
        return;
    }
    [peripheral startAdvertising:@{
        CBAdvertisementDataLocalNameKey: self.localName,
        CBAdvertisementDataServiceUUIDsKey: @[self.serviceUUID],
    }];
}

- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)peripheral error:(NSError *)error {
    if (error) {
        NSLog(@"BLE: advertising error: %@", error);
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveWriteRequests:(NSArray<CBATTRequest *> *)requests {
    for (CBATTRequest *req in requests) {
        if ([req.characteristic.UUID isEqual:self.rxUUID]) {
            NSData *data = req.value;
            if (data && data.length > 0) {
                goBLEDidReceiveWrite((void *)data.bytes, (int)data.length);
            }
        }
        if (req.characteristic.properties & CBCharacteristicPropertyWrite) {
            [peripheral respondToRequest:req withResult:CBATTErrorSuccess];
        }
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral central:(CBCentral *)central didSubscribeToCharacteristic:(CBCharacteristic *)characteristic {
    if ([characteristic.UUID isEqual:self.txUUID]) {
        self.subscribedCentral = central;
        // maximumUpdateValueLength gives the ATT MTU minus 3 bytes overhead
        self.negotiatedMTU = central.maximumUpdateValueLength;
        NSLog(@"BLE: central subscribed, MTU payload = %lu", (unsigned long)self.negotiatedMTU);
        goBLEDidSubscribe();
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral central:(CBCentral *)central didUnsubscribeFromCharacteristic:(CBCharacteristic *)characteristic {
    if ([characteristic.UUID isEqual:self.txUUID]) {
        self.subscribedCentral = nil;
        [self.txQueue removeAllObjects];
        goBLEDidUnsubscribe();
    }
}

// Called when the transmit queue has space again after updateValue returned NO.
- (void)peripheralManagerIsReadyToUpdateSubscribers:(CBPeripheralManager *)peripheral {
    [self drainTxQueue];
}

- (void)drainTxQueue {
    while (self.txQueue.count > 0) {
        NSData *chunk = self.txQueue.firstObject;
        BOOL ok = [self.manager updateValue:chunk
            forCharacteristic:self.txChar
            onSubscribedCentrals:nil];
        if (!ok) {
            // Queue is full again — peripheralManagerIsReadyToUpdateSubscribers will fire
            return;
        }
        [self.txQueue removeObjectAtIndex:0];
    }
}

@end

static VESCPeripheral *_peripheral = nil;

void bleInit(const char *name, const char *serviceUUID, const char *rxUUID, const char *txUUID) {
    _peripheral = [[VESCPeripheral alloc] init];
    _peripheral.localName = [NSString stringWithUTF8String:name];
    _peripheral.serviceUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:serviceUUID]];
    _peripheral.rxUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:rxUUID]];
    _peripheral.txUUID = [CBUUID UUIDWithString:[NSString stringWithUTF8String:txUUID]];

    dispatch_queue_t q = dispatch_queue_create("com.nosedive.ble", DISPATCH_QUEUE_SERIAL);
    _peripheral.manager = [[CBPeripheralManager alloc] initWithDelegate:_peripheral queue:q];
}

static const NSUInteger kMaxTxQueueSize = 256;

int bleSendNotification(const void *data, int len) {
    if (!_peripheral || !_peripheral.subscribedCentral || !_peripheral.txChar) {
        return -1;
    }
    NSData *nsdata = [NSData dataWithBytes:data length:len];
    BOOL ok = [_peripheral.manager updateValue:nsdata
        forCharacteristic:_peripheral.txChar
        onSubscribedCentrals:nil];
    if (!ok) {
        if (_peripheral.txQueue.count >= kMaxTxQueueSize) {
            return -2; // queue full — apply backpressure
        }
        // Queue it for later — will be drained by peripheralManagerIsReadyToUpdateSubscribers
        [_peripheral.txQueue addObject:nsdata];
    }
    return 0; // always success — queued if not sent immediately
}

int bleIsReady(void) {
    return _peripheral && _peripheral.ready ? 1 : 0;
}

int bleGetMTU(void) {
    if (!_peripheral) return 20;
    return (int)_peripheral.negotiatedMTU;
}
