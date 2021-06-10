#include "bledevice.h"

BLEDevice::BLEDevice(QObject *parent) : QObject(parent),
    currentDevice(QBluetoothDeviceInfo()),
    controller(0),
    service(0),
    serviceBatt(0)
{
    DiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    DiscoveryAgent->setLowEnergyDiscoveryTimeout(5000);

    connect(DiscoveryAgent, SIGNAL(deviceDiscovered(const QBluetoothDeviceInfo&)), this, SLOT(addDevice(const QBluetoothDeviceInfo&)));
    connect(DiscoveryAgent, SIGNAL(error(QBluetoothDeviceDiscoveryAgent::Error)), this, SLOT(deviceScanError(QBluetoothDeviceDiscoveryAgent::Error)));
    connect(DiscoveryAgent, SIGNAL(finished()), this, SLOT(scanFinished()));
}

BLEDevice::~BLEDevice()
{
    delete DiscoveryAgent;
    delete controller;
}

QStringList BLEDevice::deviceListModel()
{
    return m_deviceListModel;
}

void BLEDevice::setDeviceListModel(QStringList deviceListModel)
{
    if (m_deviceListModel == deviceListModel)
        return;

    m_deviceListModel = deviceListModel;
    emit deviceListModelChanged(m_deviceListModel);
}

void BLEDevice::resetDeviceListModel()
{
    m_deviceListModel.clear();
}

void BLEDevice::addDevice(const QBluetoothDeviceInfo &device)
{
    if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
        qDebug()<<"Discovered Device:"<<device.name()<<"Address: "<<device.address().toString()<<"RSSI:"<< device.rssi()<<"dBm";
        if(!m_foundDevices.contains(device.name(), Qt::CaseSensitive) && (device.name().contains("HTS", Qt::CaseSensitive) ||
                                                                          device.name().contains("SFT", Qt::CaseSensitive))) {
            m_foundDevices.append(device.name());
            DeviceInfo *dev = new DeviceInfo(device);
            qlDevices.append(dev);
        }
    }
}

void BLEDevice::scanFinished()
{
    setDeviceListModel(m_foundDevices);
    emit scanningFinished();
}

void BLEDevice::deviceScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    if (error == QBluetoothDeviceDiscoveryAgent::PoweredOffError)
        qDebug() << "The Bluetooth adaptor is powered off.";
    else if (error == QBluetoothDeviceDiscoveryAgent::InputOutputError)
        qDebug() << "Writing or reading from the device resulted in an error.";
    else
        qDebug() << "An unknown error has occurred.";
}

void BLEDevice::startScan()
{
    qDeleteAll(qlDevices);
    qlDevices.clear();
    m_foundDevices.clear();
    resetDeviceListModel();
    DiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    qDebug()<< "Searching for BLE devices..." ;
}

void BLEDevice::startConnect(int i)
{
    currentDevice.setDevice(((DeviceInfo*)qlDevices.at(i))->getDevice());
    if (controller) {
        controller->disconnectFromDevice();
        delete controller;
        controller = 0;

    }

    controller = new QLowEnergyController(currentDevice.getDevice(), this);
    controller ->setRemoteAddressType(QLowEnergyController::RandomAddress);

    connect(controller, SIGNAL(serviceDiscovered(QBluetoothUuid)), this, SLOT(serviceDiscovered(QBluetoothUuid)));
    connect(controller, SIGNAL(discoveryFinished()), this, SLOT(serviceScanDone()));
    connect(controller, SIGNAL(error(QLowEnergyController::Error)),  this, SLOT(controllerError(QLowEnergyController::Error)));
    connect(controller, SIGNAL(connected()), this, SLOT(deviceConnected()));
    connect(controller, SIGNAL(disconnected()), this, SLOT(deviceDisconnected()));

    controller->connectToDevice();
}

void BLEDevice::serviceDiscovered(const QBluetoothUuid &gatt)
{
    if(gatt==QBluetoothUuid(QBluetoothUuid::HealthThermometer)) {
        bFoundHTService =true;
        qDebug() << "Sensor service found";
    }
    if(gatt==QBluetoothUuid(QBluetoothUuid::BatteryService)) {
        bFoundBattService =true;
        qDebug() << "BATT service found";
    }
}

void BLEDevice::serviceScanDone()
{
    delete service;
    service=0;
    if(bFoundHTService) {
        qDebug() << "Connecting to HTS service...";
        service = controller->createServiceObject(QBluetoothUuid(QBluetoothUuid::HealthThermometer),this);
    }
    if(!service) {
        qDebug() <<"HTS service not found";
        return;
    }
    connect(service, SIGNAL(stateChanged(QLowEnergyService::ServiceState)),this,
            SLOT(serviceStateChanged(QLowEnergyService::ServiceState)));
    connect(service, SIGNAL(characteristicChanged(QLowEnergyCharacteristic, QByteArray)),this,
            SLOT(updateData(QLowEnergyCharacteristic, QByteArray)));
    connect(service, SIGNAL(descriptorWritten(QLowEnergyDescriptor, QByteArray)),this,
            SLOT(confirmedDescriptorWrite(QLowEnergyDescriptor, QByteArray)));
    service->discoverDetails();
    if(bFoundBattService) {
        qDebug() << "Connecting to Battery service...";
        serviceBatt = controller->createServiceObject(QBluetoothUuid(QBluetoothUuid::BatteryService),this);
    }
    if(!serviceBatt) {
        qDebug() <<"Batt service not found";
        return;
    }
    connect(serviceBatt, SIGNAL(stateChanged(QLowEnergyService::ServiceState)),this,
            SLOT(serviceBattStateChanged(QLowEnergyService::ServiceState)));
    connect(serviceBatt, SIGNAL(characteristicChanged(QLowEnergyCharacteristic, QByteArray)),this,
            SLOT(updateData(QLowEnergyCharacteristic, QByteArray)));
    connect(serviceBatt, SIGNAL(descriptorWritten(QLowEnergyDescriptor, QByteArray)),this,
            SLOT(confirmedDescriptorWrite(QLowEnergyDescriptor, QByteArray)));
    serviceBatt->discoverDetails();
}

quint8 BLEDevice::getBatteryLevel()
{
    quint8 batt=0;
    const QLowEnergyCharacteristic BattChar = serviceBatt->characteristic(QBluetoothUuid(QBluetoothUuid::BatteryLevel));
    if(BattChar.isValid()) {
        serviceBatt->readCharacteristic(BattChar);
        bool ok;
        batt = BattChar.value().toHex().toInt(&ok, 16);
        if(batt>100)
            batt=100;
    }
    return batt;
}

void BLEDevice::deviceDisconnected()
{
    qDebug() << "Remote device disconnected";
}

void BLEDevice::deviceConnected()
{
    qDebug() << "Device connected";
    controller->discoverServices();
}

void BLEDevice::controllerError(QLowEnergyController::Error error)
{
    qDebug() << "Controller Error:" << error;
}

void BLEDevice::serviceStateChanged(QLowEnergyService::ServiceState s)
{
    switch (s) {
    case QLowEnergyService::ServiceDiscovered:
    {
        //Temperature Measurement characteristic
        const QLowEnergyCharacteristic TempMeasChar = service->characteristic(QBluetoothUuid(QBluetoothUuid::TemperatureMeasurement));
        if (!TempMeasChar.isValid()) {
            qDebug() << "Temperature Measurement characteristic not found";
            break;
        }
        // Temperature Measurement indicate enabled
        const QLowEnergyDescriptor m_notificationDescTm = TempMeasChar.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);
        if (m_notificationDescTm.isValid()) {
            service->writeDescriptor(m_notificationDescTm, QByteArray::fromHex("0200")); // enable indication
        }
        qDebug() << "Connection Start";
        emit connectionStart();
        break;
    }
    default:
        break;
    }
}

void BLEDevice::serviceBattStateChanged(QLowEnergyService::ServiceState s)
{
    switch (s) {
    case QLowEnergyService::ServiceDiscovered:
    {
        const QLowEnergyCharacteristic BattChar = serviceBatt->characteristic(QBluetoothUuid(QBluetoothUuid::BatteryLevel));
        if (!BattChar.isValid()) {
            qDebug() << "Batt characteristic not found";
            break;
        }
        // Battery notificate enabled
        const QLowEnergyDescriptor m_notificationDesc = BattChar.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);
        if (m_notificationDesc.isValid()) {
            serviceBatt->writeDescriptor(m_notificationDesc, QByteArray::fromHex("0100")); // enable notification
        }
        break;
    }
    default:
        break;
    }
}

void BLEDevice::confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value)
{
    if (d.isValid() && d == notificationDesc && value == QByteArray("0000"))
    {
        controller->disconnectFromDevice();
        delete service;
        service = nullptr;
    }
}

void BLEDevice::writeData(QByteArray v)
{
    const QLowEnergyCharacteristic TempMeasChar = service->characteristic(QBluetoothUuid(QBluetoothUuid::TemperatureMeasurement));
    service->writeCharacteristic(TempMeasChar, v, QLowEnergyService::WriteWithoutResponse);
}

void BLEDevice::updateData(const QLowEnergyCharacteristic &c, const QByteArray &v)
{
    if (c.uuid() == QBluetoothUuid(QBluetoothUuid::TemperatureMeasurement)) {
        const uint8_t *flags = reinterpret_cast<const uint8_t*>(v.constData());
        const int32_t *temperature = reinterpret_cast<const int32_t*>(v.constData()+1);
        const uint16_t *year = reinterpret_cast<const uint16_t*>(v.constData()+5);
        const uint8_t *month = reinterpret_cast<const uint8_t*>(v.constData()+7);
        const uint8_t *day = reinterpret_cast<const uint8_t*>(v.constData()+8);
        const uint8_t *hour = reinterpret_cast<const uint8_t*>(v.constData()+9);
        const uint8_t *min = reinterpret_cast<const uint8_t*>(v.constData()+10);
        const uint8_t *sec = reinterpret_cast<const uint8_t*>(v.constData()+11);
        const uint8_t *place = reinterpret_cast<const uint8_t*>(v.constData()+12);
        // flags (0) - 0-C/1-F
        // flags (1) - TimeStamp present
        // flags (2) - TempType present
        // flags (7) - over 38 C
        // Encode ieee11073_32BitFloat_t
        int exponent = *temperature >> 24;
        int mantissa = *temperature & 0x00FFFFFF;
        qreal tempValue = (qreal)mantissa * pow(10, exponent);
        qDebug()<< v.toHex()<<*flags<<tempValue<<"\u00B0C"<<*year<<*month<<*day<<*hour<<*min<<*sec<<*place;
        QString dateTime = QString().asprintf("%04d-%02d-%02d %02d:%02d:%02d", *year, *month, *day, *hour, *min, *sec);
        QList<QVariant> data;
        data.clear();
        data.append(tempValue);
        data.append(dateTime);
        emit newData(data);
    }
    if (c.uuid() == QBluetoothUuid(QBluetoothUuid::BatteryLevel)) {
        quint8 batt=0;
        bool ok;
        batt = v.toHex().toInt(&ok, 16);
        if(batt>100)
            batt=100;
        emit batteryLevel(batt);
    }
}
