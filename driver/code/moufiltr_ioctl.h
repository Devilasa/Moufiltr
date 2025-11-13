#pragma once

// Header condiviso driver <-> user-mode per gli IOCTL del filtro mouse.
// NON includere header WDF/NT qui: deve essere neutro e includibile dall'app user-mode.

#ifdef __cplusplus
extern "C" {
#endif

// Device type privato (range user-defined 0x8000-0xFFFF)
#define FILE_DEVICE_MOUFILTR  0x8000

// codici IOCTL  che user-mode usa per parlare con il driver
#define IOCTL_MOUFILTR_GET_MODE CTL_CODE(FILE_DEVICE_MOUFILTR, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUFILTR_SET_MODE CTL_CODE(FILE_DEVICE_MOUFILTR, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUFILTR_SHUTDOWN CTL_CODE(FILE_DEVICE_MOUFILTR, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)


// Modalità di filtro
    typedef enum _MOUFILTR_MODE {
        MF_MODE_NONE = 0, // pass-through
        MF_MODE_INVERT_XY = 1, // invert X & Y
        MF_MODE_GAIN_X2 = 2, // sensitivity x2
        MF_MODE_GAIN_X4 = 3, // sensitivity x4
        MF_MODE_DEADZONE = 4, // small-motion deadzone
    } MOUFILTR_MODE;

#ifdef __cplusplus
}
#endif

