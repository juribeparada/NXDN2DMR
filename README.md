# Description

This is the source code of NXDN2DMR, a software for digital voice conversion from NXDN to DMR digital mode, based on Jonathan G4KLX's [MMDVM](https://github.com/g4klx) software.

You have to use this software with NXDNGateway, with the default NXDN UDP ports (14050 and 42022). In this case, you can select the pseudo TG20 to connect to NXDN2DMR software.

If you want to connect directly to a XLX reflector (with DMR support), you only need to uncomment ([DMR Network] section):

    XLXFile=XLXHosts.txt
    XLXReflector=950
    XLXModule=D

and replace XLXReflector and XLXModule according your preferences. Also, you need to configure the DMR port according the XLX reflector port, for example:

    Port=62030

StartupDstId, StartupPC and Address parameters don't care in XLX mode.

This software is licenced under the GPL v2 and is intended for amateur and educational use only. Use of this software for commercial purposes is strictly forbidden.
