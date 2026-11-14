#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <winsock2.h>
#include <windows.h>
#include <wlanapi.h>
bool decodeBss(const WLAN_BSS_LIST *list, QJsonArray &rows);
QJsonObject scanWifi();
