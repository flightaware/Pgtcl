# Microsoft Developer Studio Project File - Name="libpgtcl3" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libpgtcl3 - Win32 Debug Dynamic
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libpgtcl3.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libpgtcl3.mak" CFG="libpgtcl3 - Win32 Debug Dynamic"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libpgtcl3 - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libpgtcl3 - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libpgtcl3 - Win32 Debug Dynamic" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libpgtcl3 - Win32 Release Dynamic" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 1
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libpgtcl3 - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpq.lib wsock32.lib MSVCRT.lib /nologo /dll /incremental:yes /machine:I386

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpq.lib wsock32.lib MSVCRT.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug Dynamic"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "libpgtcl3___Win32_Debug_Dynamic"
# PROP BASE Intermediate_Dir "libpgtcl3___Win32_Debug_Dynamic"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug_Dynamic"
# PROP Intermediate_Dir "Debug_Dynamic"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpq.lib wsock32.lib MSVCRT.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpqdll.lib wsock32.lib  MSVCRT.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Release Dynamic"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "libpgtcl3___Win32_Release_Dynamic"
# PROP BASE Intermediate_Dir "libpgtcl3___Win32_Release_Dynamic"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release_Dynamic"
# PROP Intermediate_Dir "Release_Dynamic"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBPGTCL3_EXPORTS" /D "USE_TCL_STUBS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpq.lib wsock32.lib MSVCRT.lib /nologo /dll /incremental:yes /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib tclstub82.lib libpqdll.lib wsock32.lib  MSVCRT.lib /nologo /dll /incremental:yes /machine:I386

!ENDIF 

# Begin Target

# Name "libpgtcl3 - Win32 Release"
# Name "libpgtcl3 - Win32 Debug"
# Name "libpgtcl3 - Win32 Debug Dynamic"
# Name "libpgtcl3 - Win32 Release Dynamic"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\pgtcl.c"

!IF  "$(CFG)" == "libpgtcl3 - Win32 Release"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug Dynamic"

# PROP Intermediate_Dir "Debug_Dynamic"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Release Dynamic"

# PROP Intermediate_Dir "Release_Dynamic"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\pgtclCmds.c"

!IF  "$(CFG)" == "libpgtcl3 - Win32 Release"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug Dynamic"

# PROP Intermediate_Dir "Debug_Dynamic"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Release Dynamic"

# PROP Intermediate_Dir "Release_Dynamic"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\pgtclId.c"

!IF  "$(CFG)" == "libpgtcl3 - Win32 Release"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Debug Dynamic"

# PROP Intermediate_Dir "Debug_Dynamic"

!ELSEIF  "$(CFG)" == "libpgtcl3 - Win32 Release Dynamic"

# PROP Intermediate_Dir "Release_Dynamic"

!ENDIF 

# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\libpgtcl.h"
# End Source File
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\pgtclCmds.h"
# End Source File
# Begin Source File

SOURCE="..\..\Documents and Settings\bschwarz\My Documents\C\libpgtcl\generic\pgtclId.h"
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
