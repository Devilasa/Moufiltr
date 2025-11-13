/*--         
Copyright (c) 2008  Microsoft Corporation

Module Name:

    moufiltr.c

Abstract:

Environment:

    Kernel mode only- Framework Version 

Notes:


--*/

#include "moufiltr.h"

#include "moufiltr_ioctl.h"
#include <ntstrsafe.h> // per RtlInitUnicodeString

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, MouFilter_EvtDeviceAdd)
#pragma alloc_text (PAGE, MouFilter_EvtIoInternalDeviceControl)
#endif

#pragma warning(push)
#pragma warning(disable:4055) // type case from PVOID to PSERVICE_CALLBACK_ROUTINE
#pragma warning(disable:4152) // function/data pointer conversion in expression
// per evitare warning sulle conversioni di puntatori a funzione
 

// Aggiunte per IOCTL user-mode
//
// Flag globale atomico letto dalla ServiceCallback a DISPATCH_LEVEL
static volatile LONG g_MouFiltrMode = MF_MODE_NONE; // default: pass-through

// Control device (una sola istanza per il driver)
static WDFDEVICE g_CtlDevice = NULL;
// Guard per singola creazione (thread-safe)
static volatile LONG g_CtlCreated = 0;
// Tenerli static è più sicuro: non “inquinano” il namespace del driver.




//  DriverEntry è il "MAIN" del driver, punto di ingresso
NTSTATUS
DriverEntry (
    // IN è una macro che sta per Input, non ha effetto sul codice compilato, è un annotazione, 
    // serve ad indicare al programmatore che  l'argomento DriverObject è un parametro di input
    // l'unico vero effetto lo ha sugli strumenti di analisi del codice, lo utilizzano per rilevare potenziali errori, 
    // avvisando se la funzione tenta di modificare il parametro in un modo non previsto per un input. 
    
    // DriverObject è una struttura creata dall’ I/O Manager che rappresenta il driver nel modello WDM, 
    // I/O Manager è quello che chiama la DriverEntry quando viene caricato il driver.
    // In WDF si usa poco direttamente, WDf ci costruisce i propri oggetti sopra. (WDFDRIVER, WDFDEVICE, …)

    IN  PDRIVER_OBJECT  DriverObject,       
    IN  PUNICODE_STRING RegistryPath
    )
/*++
Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

--*/
{
    WDF_DRIVER_CONFIG   config;
    NTSTATUS            status;

    DebugPrint(("Mouse Filter Driver Sample - Driver Framework Edition.\n"));
    DebugPrint(("Built %s %s\n", __DATE__, __TIME__));
    
    // Initialize driver config to control the attributes that
    // are global to the driver. Note that framework by default
    // provides a driver unload routine. If you create any resources
    // in the DriverEntry and want to be cleaned in driver unload,
    // you can override that by manually setting the EvtDriverUnload in the
    // config structure. In general xxx_CONFIG_INIT macros are provided to
    // initialize most commonly used members.



    // Inizializza config con valori di default e registra la callback di PnP EvtDeviceAdd, chiamata cruciale
    WDF_DRIVER_CONFIG_INIT(
        &config,
        MouFilter_EvtDeviceAdd
    );

	// Override del driver unload per rimuovere il control devic, AGG_IOCTL
    config.EvtDriverUnload = MouFiltr_EvtDriverUnload;


    //
    // Create a framework driver object to represent our driver.
    //
    // Crea l’oggetto WDFDRIVER che rappresenta il tuo driver dentro il framework.
    // Collega il WDFDRIVER al DriverObject WDM passato dall’I / O Manager.
    status = WdfDriverCreate(DriverObject,
                            RegistryPath,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &config,
                            WDF_NO_HANDLE); // hDriver optional. Se non ti serve conservare l’handle del driver, puoi passare questo. In alternativa usi WDFDRIVER* per riutilizzarlo più avanti.
    // Se successo: da questo momento il driver è “agganciato” a WDF. Appena il PnP enumera device compatibili, WDF chiamerà MouFilter_EvtDeviceAdd per ciascun device.
    if (!NT_SUCCESS(status)) {
        DebugPrint( ("WdfDriverCreate failed with status 0x%x\n", status));
    }
	DebugPrint(("Exit DriverEntry\n"));
    return status; 
}





// Evt sta per evento
// Questa viene chiamata quando viene rilevato un nuovo dispositivo Plug and Play gestito dal driver
NTSTATUS
MouFilter_EvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here you can query the device properties
    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based
    on that, decide to create a filter device object and attach to the
    function stack.

    If you are not interested in filtering this particular instance of the
    device, you can just return STATUS_SUCCESS without creating a framework
    device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/   
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    NTSTATUS                            status;
    WDFDEVICE                          hDevice;
    WDF_IO_QUEUE_CONFIG        ioQueueConfig;
    
    // Evita il warning “parametro non usato” per Driver. Nessun effetto runtime.
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    DebugPrint(("Enter FilterEvtDeviceAdd \n"));

    //
    // Tell the framework that you are filter driver. Framework
    // takes care of inherting all the device flags & characterstics
    // from the lower device you are attaching to.
    //
    WdfFdoInitSetFilter(DeviceInit); // dichiariamo il device come filtro!

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes,
        DEVICE_EXTENSION);

    
    //
    // Create a framework device object.  This call will in turn create
    // a WDM deviceobject, attach to the lower stack and set the
    // appropriate flags and attributes.
    //
    // Crea il WDFDEVICE e Consuma DeviceInit
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("WdfDeviceCreate failed with status code 0x%x\n", status));
        return status;
    }

    // InterlockedExchange garantisce atomicità su CPU multi-core/IRQL vari.
    // Viene portata allo stato iniziale (pass-trough) la modalità del filtro, (impostata a NONE)
    InterlockedExchange(&g_MouFiltrMode, (LONG)MF_MODE_NONE);
    // Aggiunte per IOCTL user-mode
	//
    // Crea (una sola volta) il device di controllo per IOCTL user-mode
    (void)MouFiltr_CreateControlDevice(WdfGetDriver());



    //
    // Configure the default queue to be Parallel. Do not use sequential queue
    // if this driver is going to be filtering PS2 ports because it can lead to
    // deadlock. The PS2 port driver sends a request to the top of the stack when it
    // receives an ioctl request and waits for it to be completed. If you use a
    // a sequential queue, this request will be stuck in the queue because of the 
    // outstanding ioctl request sent earlier to the port driver.
    //
    // Crea la coda I/O di default del device con modalità Parallel.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    // Registra la callback per IRP_MJ_INTERNAL_DEVICE_CONTROL (IOCTL interni).
    // Qui intercetta IOCTL fondamentali del mouse, es. IOCTL_INTERNAL_MOUSE_CONNECT/DISCONNECT.
    ioQueueConfig.EvtIoInternalDeviceControl = MouFilter_EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            WDF_NO_HANDLE // pointer to default queue
                            );
    if (!NT_SUCCESS(status)) {
        DebugPrint( ("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }
    DebugPrint(("Exiting FilterEvtDeviceAdd\n"));
    return status;
}

// Aggiunta funzione per IOCTL user-mode
// Crea il control device per gli IOCTL user-mode (una sola istanza per driver)
//
// Crea il “control device” del tuo driver,
// cioè il device fittizio che user-mode può aprire con CreateFile("\\.\MouFiltrCtl") per mandare IOCTL.
NTSTATUS MouFiltr_CreateControlDevice(_In_ WDFDRIVER Driver)
{
    // evita creazioni multiple se EvtDeviceAdd viene chiamato per più mouse
    if (InterlockedCompareExchange(&g_CtlCreated, 1, 0) != 0)
        return STATUS_SUCCESS;

    NTSTATUS status;
    PWDFDEVICE_INIT pInit = NULL;
    WDFDEVICE dev = NULL;
    WDF_IO_QUEUE_CONFIG qcfg;
    UNICODE_STRING devName, symLink;

    // SDDL sicuro: SYSTEM + Administrators full access
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

    pInit = WdfControlDeviceInitAllocate(Driver, &sddl);
    if (!pInit) {
        InterlockedExchange(&g_CtlCreated, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitUnicodeString(&devName, L"\\Device\\MouFiltrCtl");
    status = WdfDeviceInitAssignName(pInit, &devName);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(pInit);
        InterlockedExchange(&g_CtlCreated, 0);
        return status;
    }

    status = WdfDeviceCreate(&pInit, WDF_NO_OBJECT_ATTRIBUTES, &dev);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&g_CtlCreated, 0);
        return status;
    }

    // Coda default per IOCTL user-mode
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = MouFiltr_EvtIoDeviceControl;
    status = WdfIoQueueCreate(dev, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, NULL);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(dev);
        InterlockedExchange(&g_CtlCreated, 0);
        return status;
    }

    // Symbolic link per user-mode: \\.\MouFiltrCtl
    RtlInitUnicodeString(&symLink, L"\\DosDevices\\MouFiltrCtl");
    status = WdfDeviceCreateSymbolicLink(dev, &symLink);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(dev);
        InterlockedExchange(&g_CtlCreated, 0);
        return status;
    }

    WdfControlFinishInitializing(dev);
    g_CtlDevice = dev;

    KdPrint(("Created \\Device\\MouFiltrCtl\n"));
    return STATUS_SUCCESS;
}

// Aggiunta funzione per IOCTL user-mode
//
// Driver unload: rimuove il control device se creato
VOID MouFiltr_EvtDriverUnload(_In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
    if (g_CtlDevice) {
        WdfObjectDelete(g_CtlDevice); // rimuove anche il symbolic link
        g_CtlDevice = NULL;
        InterlockedExchange(&g_CtlCreated, 0);
    }
}

// Aggiunta funzione per IOCTL user-mode
// Work item per cancellare il control device in modo asincrono (shutdown)
VOID MouFiltr_ShutdownWorkItem(IN WDFWORKITEM WorkItem)
{
    WDFDEVICE dev = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);

    KdPrint(("MouFiltr: Shutting down control device...\n"));

    if (dev) {
        WdfObjectDelete(dev);   // elimina \\Device\\MouFiltrCtl e il symbolic link
        g_CtlDevice = NULL;
        InterlockedExchange(&g_CtlCreated, 0);
    }

    WdfObjectDelete(WorkItem);
}

// Aggiunta funzione per IOCTL user-mode
//
// Gestore IOCTL user-mode
VOID MouFiltr_EvtIoDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_SUCCESS;
    size_t bytes = 0;

    switch (IoControlCode) {

    case IOCTL_MOUFILTR_GET_MODE: {
        MOUFILTR_MODE* outp = NULL;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MOUFILTR_MODE), (PVOID*)&outp, NULL);
        if (!NT_SUCCESS(status)) break;
        LONG mode = InterlockedAdd(&g_MouFiltrMode, 0);
        *outp = (MOUFILTR_MODE)mode;
        bytes = sizeof(MOUFILTR_MODE);
        break;
    }

    case IOCTL_MOUFILTR_SET_MODE: {
        MOUFILTR_MODE* inp = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(MOUFILTR_MODE), (PVOID*)&inp, NULL);
        if (!NT_SUCCESS(status)) break;

        if (*inp < MF_MODE_NONE || *inp > MF_MODE_DEADZONE) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        InterlockedExchange(&g_MouFiltrMode, (LONG)*inp);
        bytes = 0;
        break;
    }


    case IOCTL_MOUFILTR_SHUTDOWN: {

        // imposta mode default subito (forza pass-through)
        InterlockedExchange(&g_MouFiltrMode, (LONG)MF_MODE_NONE);

        // Completa subito la richiesta dell'app (non possiamo bloccarla)
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);

        // Esegui la cancellazione del control device in modo asincrono e sicuro
        // (non puoi chiamare WdfObjectDelete direttamente qui)
        WDF_WORKITEM_CONFIG workitemConfig;
        WDF_OBJECT_ATTRIBUTES workitemAttrib;
        WDFWORKITEM workitem;
        NTSTATUS wStatus;

        WDF_WORKITEM_CONFIG_INIT(&workitemConfig, MouFiltr_ShutdownWorkItem);
        WDF_OBJECT_ATTRIBUTES_INIT(&workitemAttrib);
        workitemAttrib.ParentObject = g_CtlDevice; // il parent è il control device

        wStatus = WdfWorkItemCreate(&workitemConfig,
            &workitemAttrib,
            &workitem);

        if (NT_SUCCESS(wStatus)) {
            WdfWorkItemEnqueue(workitem);
        }

        return;
    }




    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytes);
}


VOID
MouFilter_DispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
    )
/*++
Routine Description:

    Passes a request on to the lower driver.


--*/
// è un helper che viene chiamato dal tuo stesso driver, tipicamente dentro MouFilter_EvtIoInternalDeviceControl,
// per inoltrare richieste al driver sottostante senza modificarle.
{
    //
    // Pass the IRP to the target
    //
    DebugPrint(("Enter DispatchPassThrough\n"));
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status = STATUS_SUCCESS;

    //
    // We are not interested in post processing the IRP so 
    // fire and forget.
    //
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,
                                  WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus (Request);
        DebugPrint( ("WdfRequestSend failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }
    DebugPrint(("exit DispatchPassThrough\n"));
    return;
}           

VOID
MouFilter_EvtIoInternalDeviceControl(
    IN WDFQUEUE      Queue, // la coda WDF che ha ricevuto la richiesta.
    IN WDFREQUEST    Request, // la richiesta WDF (wrappa l’IRP) da gestire.
    IN size_t        OutputBufferLength, 
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode //codice dell'operazione (IOCTL)
    )
/*++

Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:
    
    IOCTL_INTERNAL_MOUSE_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.
                                      
    IOCTL_INTERNAL_I8042_HOOK_MOUSE:
        Add in the necessary function pointers and context values so that we can
        alter how the ps/2 mouse is initialized.
                                            
    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if 
           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and 
           functions to conserve space.
                                         

--*/
{
    DebugPrint(("Enter EvtIoInternalDeviceControl\n"));
    PDEVICE_EXTENSION           devExt;
    PCONNECT_DATA               connectData;
    PINTERNAL_I8042_HOOK_MOUSE  hookMouse;
    NTSTATUS                   status = STATUS_SUCCESS;
    WDFDEVICE                 hDevice;
    size_t                           length; 

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch (IoControlCode) {

    //
    // Connect a mouse class device driver to the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        //
        // Only allow one connection.
        //
        DebugPrint(("Enter case IOCTL_INTERNAL_MOUSE_CONNECT\n"));
        if (devExt->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }
        
        //
        // Copy the connection parameters to the device extension.
        //
         status = WdfRequestRetrieveInputBuffer(Request,
                            sizeof(CONNECT_DATA),
                            &connectData,
                            &length);
        if(!NT_SUCCESS(status)){
            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }

        
        devExt->UpperConnectData = *connectData;

        //
        // Hook into the report chain.  Everytime a mouse packet is reported to
        // the system, MouFilter_ServiceCallback will be called
        //
        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice); // sovrascrivi il ClassDeviceObject con il mio DEVICE_OBJECT WDM (quello del filtro);
        connectData->ClassService = MouFilter_ServiceCallback;  // sostituisci la callback con MouFilter_ServiceCallback (mia funzione).

        break;

    //
    // Disconnect a mouse class device driver from the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        DebugPrint(("Enter case IOCTL_INTERNAL_MOUSE_DISCONNECT\n"));

        //
        // Clear the connection parameters in the device extension.
        //
        // devExt->UpperConnectData.ClassDeviceObject = NULL;
        // devExt->UpperConnectData.ClassService = NULL;

        status = STATUS_NOT_IMPLEMENTED;
        break;

    //
    // Attach this driver to the initialization and byte processing of the 
    // i8042 (ie PS/2) mouse.  This is only necessary if you want to do PS/2
    // specific functions, otherwise hooking the CONNECT_DATA is sufficient
    //
    case IOCTL_INTERNAL_I8042_HOOK_MOUSE:   //PS/2 ONLY

          DebugPrint(("hook mouse received!\n"));
        
        // Get the input buffer from the request
        // (Parameters.DeviceIoControl.Type3InputBuffer)
        //
        status = WdfRequestRetrieveInputBuffer(Request,
                            sizeof(INTERNAL_I8042_HOOK_MOUSE),
                            &hookMouse,
                            &length);
        if(!NT_SUCCESS(status)){
            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }
      
        //
        // Set isr routine and context and record any values from above this driver
        //
        devExt->UpperContext = hookMouse->Context;
        hookMouse->Context = (PVOID) devExt;

        if (hookMouse->IsrRoutine) {
            devExt->UpperIsrHook = hookMouse->IsrRoutine;
        }
        hookMouse->IsrRoutine = (PI8042_MOUSE_ISR) MouFilter_IsrHook;

        //
        // Store all of the other functions we might need in the future
        //
        devExt->IsrWritePort = hookMouse->IsrWritePort;
        devExt->CallContext = hookMouse->CallContext;
        devExt->QueueMousePacket = hookMouse->QueueMousePacket;

        status = STATUS_SUCCESS;
        break;

    //
    // Might want to capture this in the future.  For now, then pass it down
    // the stack.  These queries must be successful for the RIT to communicate
    // with the mouse.
    //
    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
        DebugPrint(("Enter case IOCTL_MOUSE_QUERY_ATTRIBUTES\n"));
    default:
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return ;
    }
    DebugPrint(("exit EvtIoInternalDeviceControl\n"));
    MouFilter_DispatchPassThrough(Request,WdfDeviceGetIoTarget(hDevice));
}


BOOLEAN
MouFilter_IsrHook (
    PVOID         DeviceExtension, 
    PMOUSE_INPUT_DATA       CurrentInput, 
    POUTPUT_PACKET          CurrentOutput,
    UCHAR                   StatusByte,
    PUCHAR                  DataByte,
    PBOOLEAN                ContinueProcessing,
    PMOUSE_STATE            MouseState,
    PMOUSE_RESET_SUBSTATE   ResetSubState
)
/*++

Remarks:

    i8042prt specific code, if you are writing a packet only filter driver, you
    can remove this function

Arguments:

    DeviceExtension - Our context passed during IOCTL_INTERNAL_I8042_HOOK_MOUSE
    
    CurrentInput - Current input packet being formulated by processing all the
                    interrupts

    CurrentOutput - Current list of bytes being written to the mouse or the
                    i8042 port.
                    
    StatusByte    - Byte read from I/O port 60 when the interrupt occurred                                            
    
    DataByte      - Byte read from I/O port 64 when the interrupt occurred. 
                    This value can be modified and i8042prt will use this value
                    if ContinueProcessing is TRUE

    ContinueProcessing - If TRUE, i8042prt will proceed with normal processing of
                         the interrupt.  If FALSE, i8042prt will return from the
                         interrupt after this function returns.  Also, if FALSE,
                         it is this functions responsibilityt to report the input
                         packet via the function provided in the hook IOCTL or via
                         queueing a DPC within this driver and calling the
                         service callback function acquired from the connect IOCTL
                                             
Return Value:

    Status is returned.

  --+*/
{
    DebugPrint(("enter ouFilter_IsrHook\n"));

    PDEVICE_EXTENSION   devExt;
    BOOLEAN             retVal = TRUE;

    devExt = DeviceExtension;
    
    if (devExt->UpperIsrHook) {
        retVal = (*devExt->UpperIsrHook) (devExt->UpperContext,
                            CurrentInput,
                            CurrentOutput,
                            StatusByte,
                            DataByte,
                            ContinueProcessing,
                            MouseState,
                            ResetSubState
            );

        if (!retVal || !(*ContinueProcessing)) {
            return retVal;
        }
    }

    *ContinueProcessing = TRUE;
    return retVal;
}

    

VOID
MouFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart, // primo MOUSE_INPUT_DATA del batch.
    IN PMOUSE_INPUT_DATA InputDataEnd, // uno-past-the-end: il puntatore al primo elemento dopo l’ultimo; numero pacchetti = InputDataEnd - InputDataStart.
    IN OUT PULONG InputDataConsumed // quanti pacchetti sono stati “consumati” dal livello superiore (di solito lo imposta la callback originale che chiamiamo alla fine).
    )
/*++

Routine Description:

    Called when there are mouse packets to report to the RIT.  You can do 
    anything you like to the packets.  For instance:
    
    o Drop a packet altogether
    o Mutate the contents of a packet 
    o Insert packets into the stream 
                    
Arguments:

    DeviceObject - Context passed during the connect IOCTL
    
    InputDataStart - First packet to be reported
    
    InputDataEnd - One past the last packet to be reported.  Total number of
                   packets is equal to InputDataEnd - InputDataStart
    
    InputDataConsumed - Set to the total number of packets consumed by the RIT
                        (via the function pointer we replaced in the connect
                        IOCTL)

Return Value:

    Status is returned.

--*/
{
    DebugPrint(("enter ServiceCallback\n"));

    PDEVICE_EXTENSION   devExt;
    WDFDEVICE   hDevice;
    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    devExt = FilterGetData(hDevice);
    //
    // UpperConnectData must be called at DISPATCH
    //



    // Leggi il mode una volta sola (atomico, DISPATCH_LEVEL)
    LONG mode = InterlockedAdd(&g_MouFiltrMode, 0);
    DebugPrint(("mode:%ld \n", mode));


    switch (mode) {
    case MF_MODE_INVERT_XY:
        for (PMOUSE_INPUT_DATA cur = InputDataStart; cur != InputDataEnd; ++cur) {
            cur->LastX = -cur->LastX;
            cur->LastY = -cur->LastY;
        }
        break;

    case MF_MODE_GAIN_X2:
        for (PMOUSE_INPUT_DATA cur = InputDataStart; cur != InputDataEnd; ++cur) {
            cur->LastX = cur->LastX * 2;
            cur->LastY = cur->LastY * 2;
        }
        break;

    case MF_MODE_GAIN_X4:
        for (PMOUSE_INPUT_DATA cur = InputDataStart; cur != InputDataEnd; ++cur) {
            cur->LastX = cur->LastX * 4;
            cur->LastY = cur->LastY * 4;
        }
        break;

    case MF_MODE_DEADZONE: {
        const LONG DZ = 3; // soglia deadzone (tarabile)
        for (PMOUSE_INPUT_DATA cur = InputDataStart; cur != InputDataEnd; ++cur) {
            LONG ax = (cur->LastX < 0) ? -cur->LastX : cur->LastX;
            LONG ay = (cur->LastY < 0) ? -cur->LastY : cur->LastY;
            if (ax < DZ) cur->LastX = 0;
            if (ay < DZ) cur->LastY = 0;
        }
        break;
    }

    case MF_MODE_NONE:
    default:
        // pass-through
        break;
    }
    

	// Passa i pacchetti modificati al livello superiore
    (*(PSERVICE_CALLBACK_ROUTINE) devExt->UpperConnectData.ClassService)(
        devExt->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
        );

    DebugPrint(("exit ServiceCallback\n"));
} 

#pragma warning(pop)

//Le funzioni Interlocked* (come InterlockedExchange, InterlockedAdd, InterlockedCompareExchange, ecc.) sono primitive atomiche fornite da Windows per:
// leggere, scrivere, confrontare, aumentare / decrementare, scambiare valori
// in modo atomico, cioè non interrompibile e thread - safe.
// In kernel - mode questo è obbligatorio ogni volta che :
// due o più thread toccano la stessa variabile, oppure
// un thread e un DPC toccano la stessa variabile, oppure
// IRQL alti impediscono l’uso di mutex o lock normali.
