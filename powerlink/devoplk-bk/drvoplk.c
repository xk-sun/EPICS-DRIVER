/*this is originated from drvS7plc.c which is developed by zimoch*/

//test oplk
#include <oplk/oplk.h>
#include <limits.h>
#include <string.h>

#include <oplk/debugstr.h>

#include <system/system.h>
#include <getopt/getopt.h>
#include <console/console.h>

//#if defined(CONFIG_USE_PCAP)
#include <pcap/pcap-console.h>
//#endif

#include "app.h"
#include "xap.h"
#include "event.h"

 
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(vxWorks) || defined(__vxworks)
#include <sockLib.h>
#include <taskLib.h>
#include <selectLib.h>
#include <taskHookLib.h>
#define in_addr_t unsigned long
#else
#include <fcntl.h>
#endif

#ifdef __rtems__
#include <sys/select.h>
#endif

#include <drvSup.h>
#include <devLib.h>
#include <errlog.h>
#include <epicsVersion.h>

#include "drvoplk.h"

#if ((EPICS_VERSION==3 && EPICS_REVISION>=14) || EPICS_VERSION>3)
/* R3.14 */
#include <dbAccess.h>
#include <iocsh.h>
#include <cantProceed.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsTimer.h>
#include <epicsEvent.h>
#include <epicsExport.h>
#else
/* R3.13 */
#include "compat3_13.h"
#endif

//#define CONNECT_TIMEOUT   5.0  /* connect timeout [s] */
#define RECONNECT_DELAY  500.0  /* delay before reconnect [ms] */

// test oplk
#define CYCLE_LEN         UINT_MAX
#define NODEID            0xF0                //=> MN
#define IP_ADDR           0xc0a86401          // 192.168.100.1
#define SUBNET_MASK       0xFFFFFF00          // 255.255.255.0
#define DEFAULT_GATEWAY   0xC0A864FE          // 192.168.100.C_ADR_RT1_DEF_NODE_ID
#define DEFAULT_MAX_CYCLE_COUNT 20      // 6 is very fast
#define APP_LED_COUNT_1         8       // number of LEDs for CN1
#define APP_LED_MASK_1          (1 << (APP_LED_COUNT_1 - 1))
#define MAX_NODES               255

typedef struct
{
    UINT            leds;
    UINT            ledsOld;
    UINT            input;
    UINT            inputOld;
    UINT            period;
    int             toggle;
} APP_NODE_VAR_T;

//------------------------------------------------------------------------------
// local vars
//------------------------------------------------------------------------------
static int                  usedNodeIds_l[] = {1, 2, 3, 4, 5, 6, 7, 8, 0};
static UINT                 cnt_l;
static APP_NODE_VAR_T       nodeVar_l[MAX_NODES];
static PI_IN*               pProcessImageIn_l;
static PI_OUT*              pProcessImageOut_l;

const BYTE aMacAddr_g[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static BOOL fGsOff_l;

typedef struct
{
    char        cdcFile[256];
    char*       pLogFile;
} tOptions;

static char cvsid[] __attribute__((unused)) =
"$Id: drvS7plc.c,v 1.17 2013/01/16 10:17:33 zimoch Exp $";

//test oplk
STATIC tOplkError initPowerlink(UINT32 cycleLen_p, char* pszCdcFileName_p,const BYTE* macAddr_p,char devname[128]);
STATIC int getOptions(tOptions* pOpts_p);
STATIC long oplkIoReport(int level); 
STATIC long oplkInit();
int oplkMain (char* name);
int main();
//STATIC void oplkSendThread(oplkcnStation* station);
//STATIC void oplkReceiveThread(oplkcnStation* station);
/*STATIC int s7plcWaitForInput(s7plcStation* station, double timeout);
STATIC int s7plcEstablishConnection(s7plcStation* station);
STATIC void s7plcCloseConnection(s7plcStation* station);*/
STATIC void oplkSignal(void* event);
tOplkError processSync(void);
static tOplkError initProcessImage(void);
tOplkError initApp(void);
void shutdownPowerlink(void);
void shutdownApp(void);

oplkcnStation* oplkcnStationList = NULL;
char                 devName[128];
char* recvBuf;
char* sendBuf;
static epicsTimerQueueId timerqueue = NULL;
static short bigEndianIoc;


struct {
    long number;
    long (*report)();
    long (*init)();
} oplk = {
    2,
    oplkIoReport,
    oplkInit
};
epicsExportAddress(drvet, oplk);

int oplkDebug = 0;
epicsExportAddress(int, oplkDebug);

struct oplkcnStation {
    struct oplkcnStation* next;
    char* name;
    // char serverIP[20];
    //int serverPort;
    int inSize;
    int outSize;
    char* inBuffer;
    char* outBuffer;
    int swapBytes;
    //int connStatus;
    //volatile int socket;
    epicsMutexId mutex;
    epicsMutexId io;
    epicsTimerId timer;
    epicsEventId outTrigger;
    int outputChanged;
    IOSCANPVT inScanPvt;
    IOSCANPVT outScanPvt;
    epicsThreadId sendThread;
    epicsThreadId recvThread;
    /*float recvTimeout;
    float sendIntervall;*/
};

void oplkDebugLog(int level, const char *fmt, ...)
{
    va_list args;
    
    if (level > oplkDebug) return;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

STATIC long oplkIoReport(int level)
{
    oplkcnStation *station;

    printf("%s\n", cvsid);
    if (level == 1)
    {
        printf("oplkcn stations:\n");
       /* for (station = oplkcnStationList; station;
            station=station->next)*/
        station = oplkcnStationList;
       //{
            printf("  Station %s ", station->name);
            /*if (station->connStatus)
            {
                printf("connected via oplk to\n");
            }
            else
            {
                printf("disconnected from\n");
            }
            
            printf("  cnstation with address %s on port %d\n",
                station->serverIP, station->serverPort);*/
            printf("    inBuffer  at address %p (%d bytes)\n",
                station->inBuffer,  station->inSize);
            printf("    outBuffer at address %p (%d bytes)\n",
                station->outBuffer,  station->outSize);
           /*
           printf("    swap bytes %s\n",
                station->swapBytes
                    ? ( bigEndianIoc ? "ioc:motorola <-> plc:intel" : "ioc:intel <-> plc:motorola" )
                    : ( bigEndianIoc ? "no, both motorola" : "no, both intel" ) );
           */
          /*  printf("    receive timeout %g sec\n",
                station->recvTimeout);
            printf("    send intervall  %g sec\n",
                station->sendIntervall);*/
       // }
    }
    return 0;
}

STATIC long oplkInit()
{
    if (!oplkcnStationList) {
        errlogSevPrintf(errlogInfo,
            "oplkInit: no stations configured\n");
        return 0;
    }
    oplkDebugLog(1, "oplkInit: starting main thread\n");
    epicsThreadCreate(
        "main",
        epicsThreadPriorityMedium,
        epicsThreadGetStackSize(epicsThreadStackBig),
        (EPICSTHREADFUNC)main,
        NULL);
    return 0;
}

STATIC void oplkSignal(void* event)
{
    epicsEventSignal((epicsEventId)event);
}



static int getOptions(tOptions* pOpts_p)
{

    /* setup default parameters */
    strncpy(pOpts_p->cdcFile, "/home/test/epics/modules/powerlink/devoplk/mnobd.cdc", 256);
    pOpts_p->pLogFile = NULL;
    
    return 0;
}


int selectNIC(char* name)
{
    
    printf("%s\n", name);
    if (selectPcapDevice(devName) < 0)
        return 0;
}

static tOplkError initPowerlink(UINT32 cycleLen_p, char* pszCdcFileName_p,const BYTE* macAddr_p,char devname[128])
{
    tOplkError                  ret = kErrorOk;
    static tOplkApiInitParam    initParam;
    //static char                 devName[128];
    oplkcnStation*              station;
    //char*                       recv;
    //char*                       send;    

    printf("Initializing openPOWERLINK stack...\n");
    
    //station = oplkcnStationList;
    
    //assign memory block to receivethread and sendthread
    //recv = callocMustSucceed(1, station->inSize, "oplkReceiveThread");
    //send = callocMustSucceed(1, station->outSize, "oplkSendThread ");
    
    //recvBuf = recv;
    //sendBuf = send;
     
   /* if (selectPcapDevice(devName) < 0)
        return kErrorIllegalInstance;*/
    
    memset(&initParam, 0, sizeof(initParam));
    initParam.sizeOfInitParam = sizeof(initParam);
   

    // pass selected device name to Edrv
    initParam.hwParam.pDevName = devname;
    initParam.nodeId = NODEID;
    initParam.ipAddress = (0xFFFFFF00 & IP_ADDR) | initParam.nodeId;

    /* write 00:00:00:00:00:00 to MAC address, so that the driver uses the real hardware address */
    memcpy(initParam.aMacAddress, macAddr_p, sizeof(initParam.aMacAddress));
    
    initParam.fAsyncOnly              = FALSE;
    initParam.featureFlags            = UINT_MAX;
    initParam.cycleLen                = cycleLen_p;       // required for error detection
    initParam.isochrTxMaxPayload      = 256;              // const
    initParam.isochrRxMaxPayload      = 256;              // const
    initParam.presMaxLatency          = 50000;            // const; only required for IdentRes
    initParam.preqActPayloadLimit     = 36;               // required for initialisation (+28 bytes)
    initParam.presActPayloadLimit     = 36;               // required for initialisation of Pres frame (+28 bytes)
    initParam.asndMaxLatency          = 150000;           // const; only required for IdentRes
    initParam.multiplCylceCnt         = 0;                // required for error detection
    initParam.asyncMtu                = 1500;             // required to set up max frame size
    initParam.prescaler               = 2;                // required for sync
    initParam.lossOfFrameTolerance    = 500000;
    initParam.asyncSlotTimeout        = 3000000;
    initParam.waitSocPreq             = 1000;
    initParam.deviceType              = UINT_MAX;         // NMT_DeviceType_U32
    initParam.vendorId                = UINT_MAX;         // NMT_IdentityObject_REC.VendorId_U32
    initParam.productCode             = UINT_MAX;         // NMT_IdentityObject_REC.ProductCode_U32
    initParam.revisionNumber          = UINT_MAX;         // NMT_IdentityObject_REC.RevisionNo_U32
    initParam.serialNumber            = UINT_MAX;         // NMT_IdentityObject_REC.SerialNo_U32

    initParam.subnetMask              = SUBNET_MASK;
    initParam.defaultGateway          = DEFAULT_GATEWAY;
    sprintf((char*)initParam.sHostname, "%02x-%08x", initParam.nodeId, initParam.vendorId);
    initParam.syncNodeId              = C_ADR_SYNC_ON_SOA;
    initParam.fSyncOnPrcNode          = FALSE;
    
    
    // set callback functions
    initParam.pfnCbEvent = processEvents;
    //initParam.pfnCbSync  = NULL;
    initParam.pfnCbSync  = processSync;
    
    // initialize POWERLINK stack
    ret = oplk_init(&initParam);
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_init() failed with \"%s\" (0x%04x)\n", debugstr_getRetValStr(ret), ret);
        return ret;
    }

    ret = oplk_setCdcFilename(pszCdcFileName_p);
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_setCdcFilename() failed with \"%s\" (0x%04x)\n", debugstr_getRetValStr(ret), ret);
        return ret;
    }

    return kErrorOk;
}



int oplkcnstationConfigure(char *name, /*char* IPaddr, int port,*/ int inSize, int outSize, int bigEndian/*, int recvTimeout, int sendIntervall*/)
{
    oplkcnStation* station;
    oplkcnStation** pstation;
    //in_addr_t ip;
    
    union {short s; char c [sizeof(short)];} u;
    u.s=1;
    bigEndianIoc = !u.c[0];

// test openPOWERLINK
    UINT32                      version;
    version = oplk_getVersion();
    printf("----------------------------------------------------\n");
    printf("openPOWERLINK console MN DEMO application\n");
    printf("using openPOWERLINK Stack: %x.%x.%x\n", PLK_STACK_VER(version), PLK_STACK_REF(version), PLK_STACK_REL(version));
    printf("----------------------------------------------------\n");

    
    if (!name)
    {
        errlogSevPrintf(errlogFatal,
            "oplkConfigure: missing name\n");
        return -1;
    }
    /*if (!IPaddr)
    {
        errlogSevPrintf(errlogFatal,
            "oplkConfigure: missing IP address\n");
        return -1;
    }
    
    if (!port)
    {
        errlogSevPrintf(errlogFatal,
            "oplkConfigure: missing IP port\n");
        return -1;
    }
   
    ip = inet_addr(IPaddr);
    if (ip == INADDR_NONE)
    {
        errlogSevPrintf(errlogFatal,
            "oplkConfigure: invalid IP address %s\n", IPaddr);
        return -1;
    }
    //ip = ntohl(ip);
     */   
    

    for(pstation = &oplkcnStationList; *pstation; pstation = &(*pstation)->next);
    
    station = callocMustSucceed(1,
        sizeof(oplkcnStation) + inSize + outSize + strlen(name)+1 , "oplkcnstationConfigure");
   
    station->inSize = inSize;
    station->outSize = outSize;
    station->inBuffer = (char*)(station+1);
    station->outBuffer = (char*)(station+1)+inSize;
    station->name = (char*)(station+1)+inSize+outSize;
    strcpy(station->name, name);
    station->swapBytes = bigEndian ^ bigEndianIoc;
    //station->connStatus = 0;
    //station->socket = -1;
    station->mutex = epicsMutexMustCreate();
    station->io = epicsMutexMustCreate();
    station->outputChanged = 0;
    if (station->outSize)
    {
        station->outTrigger = epicsEventMustCreate(epicsEventEmpty);
        if (!timerqueue)
        {
            timerqueue = epicsTimerQueueAllocate(1, epicsThreadPriorityHigh);
        }
        station->timer = epicsTimerQueueCreateTimer(timerqueue,
            oplkSignal, station->outTrigger);
    }
    scanIoInit(&station->inScanPvt);
    scanIoInit(&station->outScanPvt);
    station->recvThread = NULL;
    station->sendThread = NULL;
    /*station->recvTimeout = recvTimeout > 0 ? recvTimeout/1000.0 : 2.0;
    station->sendIntervall = sendIntervall > 0 ? sendIntervall/1000.0 : 1.0;*/

    
    /* append station to list */
    *pstation = station;
     pstation = &station->next;

    return 0;
}

int oplkMain (char* name)
{
    //oplkcnStation*              station;
    tOplkError                  ret = kErrorOk;
    tOptions                    opts;
    char                        cKey = 0;
   // BOOL                        fExit = FALSE;
    //char threadname[20]

    //station = oplkcnStationList; 

     printf("%s\n", name);

     getOptions(&opts);

     if (system_init() != 0)
     {
        fprintf(stderr, "Error initializing system!");
        return 0;
     }

     initEvents(&fGsOff_l);

     if ((ret = initPowerlink(CYCLE_LEN, opts.cdcFile, aMacAddr_g, devName)) != kErrorOk)
     printf("initPowerlink occur error\n");

     if ((ret = initApp()) != kErrorOk)
     printf("initApp occur error\n");

    ret = oplk_execNmtCommand(kNmtEventSwReset);
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_execNmtCommand() failed with \"%s\" (0x%04x)\n",
                debugstr_getRetValStr(ret), ret);
        return 0;
    }
 
    
    return 0;
}


#if (EPICS_REVISION>=14)
static const iocshArg oplkcnstationConfigureArg0 = { "name", iocshArgString };
//static const iocshArg oplkcnstationConfigureArg1 = { "IPaddr", iocshArgString };
//static const iocshArg oplkcnstationConfigureArg2 = { "IPport", iocshArgInt };
static const iocshArg oplkcnstationConfigureArg1 = { "inSize", iocshArgInt };
static const iocshArg oplkcnstationConfigureArg2 = { "outSize", iocshArgInt };
static const iocshArg oplkcnstationConfigureArg3 = { "bigEndian", iocshArgInt };
//static const iocshArg oplkcnstationConfigureArg6 = { "recvTimeout", iocshArgInt };
//static const iocshArg oplkcnstationConfigureArg7 = { "sendIntervall", iocshArgInt };
static const iocshArg * const oplkcnstationConfigureArgs[] = {
    &oplkcnstationConfigureArg0,
    &oplkcnstationConfigureArg1,
    &oplkcnstationConfigureArg2,
    &oplkcnstationConfigureArg3
   /* &oplkcnstationConfigureArg4,
    &oplkcnstationConfigureArg5,
    &oplkcnstationConfigureArg6,
    &oplkcnstationConfigureArg7*/
};
static const iocshFuncDef oplkcnstationConfigureDef = { "oplkcnstationConfigure", 4, oplkcnstationConfigureArgs };
static void oplkcnstationConfigureFunc (const iocshArgBuf *args)
{
    int status = oplkcnstationConfigure(
        args[0].sval, args[1].ival, args[2].ival,
        args[3].ival/*, args[4].ival, args[5].ival,
        args[6].ival, args[7].ival*/);
        
    if (status) exit(1);
}


static const iocshArg selectNICArg0 = { "name", iocshArgString };
static const iocshArg * const selectNICArgs[] = {
    &selectNICArg0
};
static const iocshFuncDef selectNICDef = { "selectNIC", 1, selectNICArgs };
static void selectNICFunc (const iocshArgBuf *args)
{
    int status = selectNIC(
        args[0].sval);

    if (status) exit(1);
}



static const iocshArg oplkMainArg0 = { "name", iocshArgString };
static const iocshArg * const oplkMainArgs[] = {
    &oplkMainArg0
};
static const iocshFuncDef oplkMainDef = { "oplkMain", 1, selectNICArgs };
static void oplkMainFunc (const iocshArgBuf *args)
{
    int status = oplkMain(
        args[0].sval);

    if (status) exit(1);
}





static void oplkRegister ()
{
    iocshRegister(&oplkcnstationConfigureDef, oplkcnstationConfigureFunc);
    iocshRegister(&selectNICDef,selectNICFunc);
    iocshRegister(&oplkMainDef,oplkMainFunc);

}  

epicsExportRegistrar(oplkRegister);
#endif

oplkcnStation *oplkOpen(char *name)
{
    oplkcnStation *station;

    for (station = oplkcnStationList; station; station = station->next)
    {
        if (strcmp(name, station->name) == 0)
        {
            return station;
        }
    }
    errlogSevPrintf(errlogFatal,
        "oplkOpen: station %s not found\n", name);
    return NULL;
}

IOSCANPVT oplkGetInScanPvt(oplkcnStation *station)
{
    return station->inScanPvt;
}

IOSCANPVT oplkGetOutScanPvt(oplkcnStation *station)
{
    return station->outScanPvt;
}

int oplkReadArray(
    oplkcnStation *station,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* data
)
{
    unsigned int elem, i;
    unsigned char byte;
    //epicsUInt16 connStatus;

    if (offset+dlen > station->inSize)
    {
       errlogSevPrintf(errlogMajor,
        "oplkRead %s/%u: offset out of range\n",
        station->name, offset);
       return S_drv_badParam;
    }
    if (offset+nelem*dlen > station->inSize)
    {
       errlogSevPrintf(errlogMajor, 
        "oplkRead %s/%u: too many elements (%u)\n",
        station->name, offset, nelem);
       return S_drv_badParam;
    }
    oplkDebugLog(4,
        "oplkReadArray (station=%p, offset=%u, dlen=%u, nelem=%u)\n",
        station, offset, dlen, nelem);
    epicsMutexMustLock(station->mutex);
    //connStatus = station->connStatus;
    for (elem = 0; elem < nelem; elem++)
    {
        oplkDebugLog(5, "data in:");
        for (i = 0; i < dlen; i++)
        {
            if (station->swapBytes)
                byte = station->inBuffer[offset + elem*dlen + dlen - 1 - i];
            else
                byte = station->inBuffer[offset + elem*dlen + i];
            ((char*)data)[elem*dlen+i] = byte;
            oplkDebugLog(5, " %02x", byte);
        }
        oplkDebugLog(5, "\n");
    }    
    epicsMutexUnlock(station->mutex);
    //if (!connStatus) return S_drv_noConn;
    return S_drv_OK;
}

int oplkWriteMaskedArray(
    oplkcnStation *station,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* data,
    void* mask
)
{
    unsigned int elem, i;
    unsigned char byte;
    //epicsUInt16 connStatus;
    //mask = NULL;
    if (offset+dlen > station->outSize)
    {
        errlogSevPrintf(errlogMajor,
            "oplkWrite %s/%d: offset out of range\n",
            station->name, offset);
        return -1;
    }
    if (offset+nelem*dlen > station->outSize)
    {
        errlogSevPrintf(errlogMajor,
            "oplk %s/%d: too many elements (%u)\n",
            station->name, offset, nelem);
        return -1;
    }
    oplkDebugLog(4,
        "oplkWriteMaskedArray (station=%p, offset=%u, dlen=%u, nelem=%u)\n",
        station, offset, dlen, nelem);
    epicsMutexMustLock(station->mutex);
    //connStatus = station->connStatus;
    //printf("dlen = %d\n",dlen);
    //printf("nelem = %d\n",nelem);
    for (elem = 0; elem < nelem; elem++)
    {
        oplkDebugLog(5, "data out:");
        for (i = 0; i < dlen; i++)
        {
            byte = ((unsigned char*)data)[elem*dlen+i];
            //printf("byte = %d\n",byte);
            if (mask)
            {
                oplkDebugLog(5, "(%02x & %02x)",
                    byte, ((unsigned char*)mask)[i]);
                byte &= ((unsigned char*)mask)[i];
            }
            if (station->swapBytes)
            {
                if (mask)
                {
                    oplkDebugLog(5, " | (%02x & %02x) =>",
                        station->outBuffer[offset + elem*dlen + dlen - 1 - i],
                        ~((unsigned char*)mask)[i]);
                    byte |=
                        station->outBuffer[offset + elem*dlen + dlen - 1 - i]
                        & ~((unsigned char*)mask)[i];
                }
                //oplkDebugLog(5, " %02x", byte);
                
                //printf("byte = %d\t",byte);
                //printf("offset = %d\t",offset);
                //printf("elem = %d\t",elem);
                //printf("dlen = %d\t",dlen);
                //printf("i = %d\t",i);
                //printf("mask = %x\t",mask);
                station->outBuffer[offset + elem*dlen + dlen - 1 - i] = byte;
                //printf("index = %d\t",offset + elem*dlen + dlen - 1 - i);
                //printf("outBuffer = %d\n",station->outBuffer[0]);
                //printf("outBuffer = %d\n",station->outBuffer[offset + elem*dlen + dlen - 1 - i]);
            }
            else
            {
                if (mask)
                {
                    oplkDebugLog(5, " | (%02x & %02x) =>",
                        station->outBuffer[offset + elem*dlen + i],
                        ~((unsigned char*)mask)[i]);
                    byte |= station->outBuffer[offset + elem*dlen + i]
                        & ~((unsigned char*)mask)[i];
                }
                oplkDebugLog(5, " %02x", byte);
                station->outBuffer[offset + elem*dlen + i] = byte;
            }
        }
        oplkDebugLog(5, "\n");
        station->outputChanged=1;
    }    
    epicsMutexUnlock(station->mutex);
    //if (!connStatus) return S_drv_noConn;
    return S_drv_OK;
}

int oplkWriteao(
    oplkcnStation *station,
    unsigned int offset,
    unsigned int dlen,
    unsigned int nelem,
    void* data
    )
{
    unsigned int elem, i;
    unsigned char byte;
    //epicsUInt16 connStatus;
    //mask = NULL;
    if (offset+dlen > station->outSize)
    {
        errlogSevPrintf(errlogMajor,
            "oplkWrite %s/%d: offset out of range\n",
            station->name, offset);
        return -1;
    }
    if (offset+nelem*dlen > station->outSize)
    {
        errlogSevPrintf(errlogMajor,
            "oplk %s/%d: too many elements (%u)\n",
            station->name, offset, nelem);

          return -1;
    }
    oplkDebugLog(4,
        "oplkWriteMaskedArray (station=%p, offset=%u, dlen=%u, nelem=%u)\n",
        station, offset, dlen, nelem);
    epicsMutexMustLock(station->mutex);
    //connStatus = station->connStatus;
    //printf("dlen = %d\n",dlen);
    //printf("nelem = %d\n",nelem);
    for (elem = 0; elem < nelem; elem++)
    {
        oplkDebugLog(5, "data out:");
        for (i = 0; i < dlen; i++)
        {
            byte = ((unsigned char*)data)[elem*dlen+i];
            //printf("byte = %d\n",byte);
            /*if (mask)
            {
                oplkDebugLog(5, "(%02x & %02x)",
                    byte, ((unsigned char*)mask)[i]);
                byte &= ((unsigned char*)mask)[i];
            }*/
            if (station->swapBytes)
            {
               /* if (mask)
                {
                    oplkDebugLog(5, " | (%02x & %02x) =>",
                        station->outBuffer[offset + elem*dlen + dlen - 1 - i],
                        ~((unsigned char*)mask)[i]);
                    byte |=
                        station->outBuffer[offset + elem*dlen + dlen - 1 - i]
                        & ~((unsigned char*)mask)[i];

                 }*/
                //oplkDebugLog(5, " %02x", byte);

               // printf("byte = %d\t",byte);
               // printf("offset = %d\t",offset);
               // printf("elem = %d\t",elem);
               // printf("dlen = %d\t",dlen);
               // printf("i = %d\t",i);
               // printf("mask = %x\t",mask);
                station->outBuffer[offset + elem*dlen + dlen - 1 - i] = byte;
               // printf("index = %d\t",offset + elem*dlen + dlen - 1 - i);
               // printf("outBuffer = %d\n",station->outBuffer[offset + elem*dlen + dlen - 1 - i]);
    //printf(" write outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("write outBuffer[1] = %d\n",station->outBuffer[1]);
                //printf("outBuffer = %d\n",station->outBuffer[offset + elem*dlen + dlen - 1 - i]);
            }
            else
            {
               /* if (mask)
                {
                    oplkDebugLog(5, " | (%02x & %02x) =>",
                        station->outBuffer[offset + elem*dlen + i],
                        ~((unsigned char*)mask)[i]);
                    byte |= station->outBuffer[offset + elem*dlen + i]
                        & ~((unsigned char*)mask)[i];
                }*/
                oplkDebugLog(5, " %02x", byte);
                station->outBuffer[offset + elem*dlen + i] = byte;
            }
        }
        oplkDebugLog(5, "\n");
        station->outputChanged=1;
    }
    epicsMutexUnlock(station->mutex);
    //if (!connStatus) return S_drv_noConn;
    return S_drv_OK;
}

/*
int oplkMain (char* name)
{
    //oplkcnStation*              station;
    tOplkError                  ret = kErrorOk;
    tOptions                    opts;
    char                        cKey = 0;
    BOOL                        fExit = FALSE;
    //char threadname[20]
    
    //station = oplkcnStationList; 
     
     printf("%s\n", name);

     getOptions(&opts);

     if (system_init() != 0)
     {
        fprintf(stderr, "Error initializing system!");
        return ;
     }

     initEvents(&fGsOff_l);

     if ((ret = initPowerlink(CYCLE_LEN, opts.cdcFile, aMacAddr_g, devName)) != kErrorOk)
     printf("initPowerlink occur error\n");

     if ((ret = initApp()) != kErrorOk)
     printf("initApp occur error\n");
     
     return 0;
}
 */    



int main ()
{
  // tOplkError                  ret = kErrorOk;
   BOOL                          fExit = FALSE;
   oplkDebugLog(1, "oplkMain: main thread started\n");
    
   // start stack processing by sending a NMT reset command
  /*  ret = oplk_execNmtCommand(kNmtEventSwReset);
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_execNmtCommand() failed with \"%s\" (0x%04x)\n",
                debugstr_getRetValStr(ret), ret);
        return 0;
    }*/

    printf("\n-------------------------------\n");
   // printf("Press Esc to leave the program\n");
   // printf("Press r to reset the node\n");
    printf("-------------------------------\n\n");

    while (!fExit)
    {   
                    
      /*  if (console_kbhit())
        {
            cKey = (char)console_getch();
            switch (cKey)
            {
                case 'r':
                    ret = oplk_execNmtCommand(kNmtEventSwReset);
                    if (ret != kErrorOk)
                    {
                        fprintf(stderr, "oplk_execNmtCommand() failed with \"%s\" (0x%04x)\n",
                                debugstr_getRetValStr(ret), ret);
                        fExit = TRUE;
                    }
                    break;

                case 'c':
                    ret = oplk_execNmtCommand(kNmtEventNmtCycleError);
                    if (ret != kErrorOk)
                    {
                        fprintf(stderr, "oplk_execNmtCommand() failed with \"%s\" (0x%04x)\n",
                                debugstr_getRetValStr(ret), ret);
                        fExit = TRUE;
                    }
                    break;

                case 0x1B:
                    fExit = TRUE;
                    break;

                default:
                    break;
            }
        }

        if (system_getTermSignalState() == TRUE)
        {
            fExit = TRUE;
            printf("Received termination signal, exiting...\n");
        }*/

        if (oplk_checkKernelStack() == FALSE)
        {
            fExit = TRUE;
            fprintf(stderr, "Kernel stack has gone! Exiting...\n");
        }

       #if defined(CONFIG_USE_SYNCTHREAD) || defined(CONFIG_KERNELSTACK_DIRECTLINK)
        system_msleep(RECONNECT_DELAY);
       #else
        processSync();
       #endif

    } 

    return 0;
}


tOplkError initApp(void)
{
    tOplkError ret = kErrorOk;
    int        i;

    cnt_l = 0;

    for (i = 0; (i < MAX_NODES) && (usedNodeIds_l[i] != 0); i++)
    {
        nodeVar_l[i].leds = 0;
        nodeVar_l[i].ledsOld = 0;
        nodeVar_l[i].input = 0;
        nodeVar_l[i].inputOld = 0;
        nodeVar_l[i].toggle = 0;
        nodeVar_l[i].period = 0;
    }

    ret = initProcessImage();

    return ret;
}

//------------------------------------------------------------------------------
/**
\brief  Shutdown the synchronous data application

The function shuts down the synchronous data application

\return The function returns a tOplkError error code.

\ingroup module_demo_mn_console
*/
//------------------------------------------------------------------------------

void shutdownApp(void)
{
    tOplkError          ret = kErrorOk;

    ret = oplk_freeProcessImage();
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_freeProcessImage() failed with \"%s\" (0x%04x)\n",
                debugstr_getRetValStr(ret), ret);
    }
}

//------------------------------------------------------------------------------
/**
\brief  Synchronous data handler

The function implements the synchronous data handler.

\return The function returns a tOplkError error code.


\ingroup module_demo_mn_console
*/
//------------------------------------------------------------------------------
tOplkError processSync(void)
{
    
    oplkcnStation*      station;
    tOplkError          ret = kErrorOk;
    char  *name1,*name2,*name3,*name4,*name5,*name6,*name7,*name8;
    name1 = "testcn:1";
    name2 = "testcn:2";
    name3 = "testcn:3";
    name4 = "testcn:4";
    name5 = "testcn:5";
    name6 = "testcn:6";
    name7 = "testcn:7";
    name8 = "testcn:8";
     
    //station = oplkcnStationList;
    
    //printf("test\n");
    
    //char*  RecvBuf = recvBuf;
    //char RecvBuf[station->inSize];
    //char*  SendBuf = sendBuf;
    //char SendBuf[station->outSize];
    
    ret = oplk_waitSyncEvent(100000);
    if (ret != kErrorOk)
        return ret;

    //receivethread started
    
    ret = oplk_exchangeProcessImageOut();
    if (ret != kErrorOk)
        return ret;
     
    for(station = oplkcnStationList; station; station = station->next)
    {
    
     if(strcmp(station->name, name1) == 0) 
     {
      char cn1RecvBuf[station->inSize];
      char cn1SendBuf[station->outSize];

      nodeVar_l[0].input = pProcessImageOut_l->CN1_M00_DigitalInput_00h_AU8_DigitalInput01;
      nodeVar_l[1].input = pProcessImageOut_l->CN1_M00_DigitalInput_00h_AU8_DigitalInput02;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/
    
     //printf("nodeVar_l[0].input %d\n",nodeVar_l[0].input);
     //printf("nodeVar_l[1].input %d\n",nodeVar_l[1].input);
    
      cn1RecvBuf[0] = nodeVar_l[0].input;
      cn1RecvBuf[1] = nodeVar_l[1].input;   
    
      epicsMutexMustLock(station->mutex);
      memcpy(station->inBuffer, cn1RecvBuf, station->inSize);
      epicsMutexUnlock(station->mutex);
    
      scanIoRequest(station->inScanPvt);
    

    //sendthread started
    //epicsTimerStartDelay(station->timer, station->sendIntervall);
    
     if(station->outputChanged)
     {
       epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
       memcpy(cn1SendBuf, station->outBuffer, station->outSize);
       station->outputChanged = 0; 
       epicsMutexUnlock(station->mutex);
    
       nodeVar_l[0].leds = cn1SendBuf[0];
       nodeVar_l[1].leds = cn1SendBuf[1];
    
    //printf("output-1 = %d\n",nodeVar_l[0].leds);
       pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput01 = nodeVar_l[0].leds;
       pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
     }
     ret = oplk_exchangeProcessImageIn();
     scanIoRequest(station->outScanPvt);
     
    continue;
    }
      
    else if(strcmp(station->name, name2) == 0)
    {
    char cn2RecvBuf[station->inSize];
    char cn2SendBuf[station->outSize];

   // printf("name is %s\n",station->name);
    nodeVar_l[2].input = pProcessImageOut_l->CN2_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn2RecvBuf[0] = nodeVar_l[2].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;
    //printf("nodeVar_l[2].input = %d\n",cn2RecvBuf[0]);
    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn2RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn2SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[2].leds = cn2SendBuf[0];
    //nodeVar_l[2].leds = 2;
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN2_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);

    continue;
    }
    
     
    else if(strcmp(station->name, name3) == 0)
    {
    char cn3RecvBuf[station->inSize];
    char cn3SendBuf[station->outSize];

   // printf("name is %s\n",station->name);
    nodeVar_l[3].input = pProcessImageOut_l->CN3_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn3RecvBuf[0] = nodeVar_l[3].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn3RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn3SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[3].leds = cn3SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN3_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[3].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);

    continue;
    }
    
    
    else if(strcmp(station->name, name4) == 0)
    {
    char cn4RecvBuf[station->inSize];
    char cn4SendBuf[station->outSize];

    nodeVar_l[4].input = pProcessImageOut_l->CN4_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn4RecvBuf[0] = nodeVar_l[4].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn4RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn4SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[4].leds = cn4SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN4_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[4].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);

    continue;
    }
    
    
    else if(strcmp(station->name, name5) == 0)
    {
    char cn5RecvBuf[station->inSize];
    char cn5SendBuf[station->outSize];

    nodeVar_l[5].input = pProcessImageOut_l->CN5_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn5RecvBuf[0] = nodeVar_l[5].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn5RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn5SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[5].leds = cn5SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    //nodeVar_l[6].input = pProcessImageOut_l->CN6_M00_DigitalInput_00h_AU8_DigitalInput;
   // pProcessImageIn_l->CN5_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[6].input;
    pProcessImageIn_l->CN5_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[5].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);

    continue;
    }

    
    else if(strcmp(station->name, name6) == 0)
    {
    char cn6RecvBuf[station->inSize];
    char cn6SendBuf[station->outSize];

    nodeVar_l[6].input = pProcessImageOut_l->CN6_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn6RecvBuf[0] = nodeVar_l[6].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;
    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn6RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn6SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[6].leds = cn6SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    //pProcessImageIn_l->CN6_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[6].leds;
    pProcessImageIn_l->CN6_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[6].input;
    //pProcessImageIn_l->CN6_M00_DigitalOutput_00h_AU8_DigitalOutput = 2;
    //printf("input= %d\n",nodeVar_l[6].input);
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);
    
    continue;
    }

    
    else if(strcmp(station->name, name7) == 0)
    {
    char cn7RecvBuf[station->inSize];
    char cn7SendBuf[station->outSize];

    nodeVar_l[7].input = pProcessImageOut_l->CN7_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn7RecvBuf[0] = nodeVar_l[7].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn7RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn7SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[7].leds = cn7SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN7_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[7].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);
    
    continue;
    }

    
    else if(strcmp(station->name, name8) == 0)
    {
    char cn8RecvBuf[station->inSize];
    char cn8SendBuf[station->outSize];

    nodeVar_l[8].input = pProcessImageOut_l->CN8_M00_DigitalInput_00h_AU8_DigitalInput;
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/

    //*RecvBuf = nodeVar_l[0].input;
    cn8RecvBuf[0] = nodeVar_l[8].input;
    //cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn8RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn8SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[8].leds = cn8SendBuf[0];
    //nodeVar_l[1].leds = cn1SendBuf[1];

    //printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN8_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[8].leds;
    //pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);
    }

   }

} 
    



   /* if(strcmp(station->name, name) == 0)
    {
    char cn1RecvBuf[station->inSize];
    char cn1SendBuf[station->outSize];

    nodeVar_l[0].input = pProcessImageOut_l->CN1_M00_DigitalInput_00h_AU8_DigitalInput01;
    nodeVar_l[1].input = pProcessImageOut_l->CN1_M00_DigitalInput_00h_AU8_DigitalInput02;*/
    //nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;*/
    
    //*RecvBuf = nodeVar_l[0].input;
    /*cn1RecvBuf[0] = nodeVar_l[0].input;
    cn1RecvBuf[1] = nodeVar_l[1].input;   
    
    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn1RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);
    
    scanIoRequest(station->inScanPvt);*/
    

    //sendthread started
    //epicsTimerStartDelay(station->timer, station->sendIntervall);
    
    
    /*if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);*/
    //printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    //printf("outBuffer[1] = %d\n",station->outBuffer[1]);
   /* memcpy(cn1SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0; 
    epicsMutexUnlock(station->mutex);
    
    nodeVar_l[0].leds = cn1SendBuf[0];
    nodeVar_l[1].leds = cn1SendBuf[1];*/
    
    //printf("output-1 = %d\n",nodeVar_l[0].leds);
   // pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput01 = nodeVar_l[0].leds;
   // pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    //pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
   // }
   // ret = oplk_exchangeProcessImageIn();
    //scanIoRequest(station->outScanPvt);
    //epicsEventMustWait(station->outTrigger);
    //} 

   /* else
    {
    char cn2RecvBuf[station->inSize];
    char cn2SendBuf[station->outSize];

    nodeVar_l[2].input = pProcessImageOut_l->CN2_M00_DigitalInput_00h_AU8_DigitalInput;
    nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;

    RecvBuf = nodeVar_l[0].input;
    cn2RecvBuf[0] = nodeVar_l[2].input;
    cn1RecvBuf[1] = nodeVar_l[1].input;

    epicsMutexMustLock(station->mutex);
    memcpy(station->inBuffer, cn2RecvBuf, station->inSize);
    epicsMutexUnlock(station->mutex);

    scanIoRequest(station->inScanPvt);
    
    if(station->outputChanged)
    {
    epicsMutexMustLock(station->mutex);
    printf("outBuffer[0] = %d\n",station->outBuffer[0]);
    printf("outBuffer[1] = %d\n",station->outBuffer[1]);
    memcpy(cn2SendBuf, station->outBuffer, station->outSize);
    station->outputChanged = 0;
    epicsMutexUnlock(station->mutex);

    nodeVar_l[2].leds = cn2SendBuf[0];
    nodeVar_l[1].leds = cn1SendBuf[1];

    printf("output-1 = %d\n",nodeVar_l[0].leds);
    pProcessImageIn_l->CN2_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds;
    pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput02 = nodeVar_l[1].leds;
    pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds; 
    }
    ret = oplk_exchangeProcessImageIn();
    scanIoRequest(station->outScanPvt);
    epicsEventMustWait(station->outTrigger);
    }


   }
    
    return ret;
}
*/




/*tOplkError processSync(void)
{
    tOplkError          ret = kErrorOk;
  //  int                 i;
  //  int                 c;

  //  c = recvBuf;
  //  printf("c is %d",c);

 //  printf("test is %d",test);
 //   printf("processSync start\n");
    ret = oplk_waitSyncEvent(100000);
    if (ret != kErrorOk)
        return ret;

    ret = oplk_exchangeProcessImageOut();
    if (ret != kErrorOk)
        return ret;

    cnt_l++;
   // printf("cnt_l = %d\n",cnt_l);
    nodeVar_l[0].input = pProcessImageOut_l->CN1_M00_DigitalInput_00h_AU8_DigitalInput;
    printf("input is %c\n", nodeVar_l[0].input);
    nodeVar_l[1].input = pProcessImageOut_l->CN32_M00_DigitalInput_00h_AU8_DigitalInput;
    nodeVar_l[2].input = pProcessImageOut_l->CN110_M00_DigitalInput_00h_AU8_DigitalInput;
    
    nodeVar_l[0].leds = cnt_l;
    pProcessImageIn_l->CN1_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[0].leds;
    pProcessImageIn_l->CN32_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[1].leds;
    pProcessImageIn_l->CN110_M00_DigitalOutput_00h_AU8_DigitalOutput = nodeVar_l[2].leds;

    ret = oplk_exchangeProcessImageIn();

    return ret;
}*/


static tOplkError initProcessImage(void)
{
    tOplkError      ret = kErrorOk;

    printf("Initializing process image...\n");
    printf("Size of input process image: %lu\n", (ULONG)sizeof(PI_IN));
    printf("Size of output process image: %lu\n", (ULONG)sizeof(PI_OUT));
    ret = oplk_allocProcessImage(sizeof(PI_IN), sizeof(PI_OUT));
    if (ret != kErrorOk)
    {
        return ret;
    }

    pProcessImageIn_l = (PI_IN*)oplk_getProcessImageIn();
    pProcessImageOut_l = (PI_OUT*)oplk_getProcessImageOut();

    ret = oplk_setupProcessImage();

    return ret;
}

void shutdownPowerlink(void)
{
    UINT                i;
    tOplkError          ret = kErrorOk;

    // NMT_GS_OFF state has not yet been reached
    fGsOff_l = FALSE;

#if !defined(CONFIG_KERNELSTACK_DIRECTLINK) && defined(CONFIG_USE_SYNCTHREAD)
    system_stopSyncThread();
    system_msleep(100);
#endif

    // halt the NMT state machine so the processing of POWERLINK frames stops
    ret = oplk_execNmtCommand(kNmtEventSwitchOff);
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_execNmtCommand() failed with \"%s\" (0x%04x)\n",
                debugstr_getRetValStr(ret), ret);
    }

    // small loop to implement timeout waiting for thread to terminate
    for (i = 0; i < 1000; i++)
    {
        if (fGsOff_l)
            break;
    }

    printf("Stack is in state off ... Shutdown\n");
    ret = oplk_shutdown();
    if (ret != kErrorOk)
    {
        fprintf(stderr, "oplk_shutdown() failed with \"%s\" (0x%04x)\n",
                debugstr_getRetValStr(ret), ret);
    }
}

