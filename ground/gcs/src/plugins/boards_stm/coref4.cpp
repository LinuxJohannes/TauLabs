/**
 ******************************************************************************
 *
 * @file       coref4.cpp
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2014
 *
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup Boards_Stm STM boards support Plugin
 * @{
 * @brief Plugin to support boards by STM
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "coref4.h"

#include <uavobjectmanager.h>
#include "uavobjectutil/uavobjectutilmanager.h"
#include <extensionsystem/pluginmanager.h>

#include "hwcoref4.h"
/**
 * @brief Quanton::Quanton
 *  This is the Quanton board definition
 */
CoreF4::CoreF4(void)
{
    // Initialize our USB Structure definition here:
    USBInfo board;
    board.vendorID = 0x20A0;
    board.productID = 0x415b;

    setUSBInfo(board);

    boardType = 0x89;

    // Define the bank of channels that are connected to a given timer
    channelBanks.resize(6);
    channelBanks[0] = QVector<int> () << 1 << 2 << 3 << 4;
    channelBanks[1] = QVector<int> () << 5 << 6 << 7 << 8;
}

CoreF4::~CoreF4()
{

}

QString CoreF4::shortName()
{
    return QString("coref4");
}

QString CoreF4::boardDescription()
{
    return QString("CoreF4");
}

//! Return which capabilities this board has
bool CoreF4::queryCapabilities(BoardCapabilities capability)
{
    switch(capability) {
    case BOARD_CAPABILITIES_GYROS:
        return true;
    case BOARD_CAPABILITIES_ACCELS:
        return true;
    case BOARD_CAPABILITIES_MAGS:
        return true;
    case BOARD_CAPABILITIES_BAROS:
        return true;
    case BOARD_CAPABILITIES_RADIO:
        return false;
    }
    return false;
}


/**
 * @brief CoreF4::getSupportedProtocols
 *  TODO: this is just a stub, we'll need to extend this a lot with multi protocol support
 * @return
 */
QStringList CoreF4::getSupportedProtocols()
{

    return QStringList("uavtalk");
}

QPixmap CoreF4::getBoardPicture()
{
    return QPixmap();
}

QString CoreF4::getHwUAVO()
{
    return "HwCoreF4";
}

int CoreF4::queryMaxGyroRate()
{
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    UAVObjectManager *uavoManager = pm->getObject<UAVObjectManager>();
    HwCoreF4 *hwCoreF4 = HwCoreF4::GetInstance(uavoManager);
    Q_ASSERT(hwCoreF4);
    if (!hwCoreF4)
        return 0;

    HwCoreF4::DataFields settings = hwCoreF4->getData();

    switch(settings.GyroRange) {
    case HwCoreF4::GYRORANGE_250:
        return 250;
    case HwCoreF4::GYRORANGE_500:
        return 500;
    case HwCoreF4::GYRORANGE_1000:
        return 1000;
    case HwCoreF4::GYRORANGE_2000:
        return 2000;
    default:
        return 500;
    }
}
