!macro customInstall
  ${ifNot} ${isUpdated}
    IfFileExists "$INSTDIR\resources\native\pulsefx_device_setup.exe" pulsefx_helper_present pulsefx_helper_missing

    pulsefx_helper_present:
      DetailPrint "Creating PulseFX virtual audio device..."
      nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" install'
      Pop $0
      Pop $1
      ${If} $0 != 0
        DetailPrint "Device setup output: $1"
        MessageBox MB_ICONSTOP|MB_OK "PulseFX could not create its Windows virtual audio device (exit code $0). Setup cannot continue."
        Abort
      ${EndIf}

      IfFileExists "$INSTDIR\resources\driver\*.inf" pulsefx_driver_present pulsefx_driver_missing

    pulsefx_driver_present:
      DetailPrint "Installing PulseFX virtual audio driver..."
      nsExec::ExecToStack '"$SYSDIR\pnputil.exe" /add-driver "$INSTDIR\resources\driver\*.inf" /subdirs /install'
      Pop $0
      Pop $1
      ${If} $0 != 0
        DetailPrint "PnPUtil output: $1"
        DetailPrint "Removing unbound PulseFX virtual device..."
        nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" remove'
        Pop $2
        Pop $3
        MessageBox MB_ICONSTOP|MB_OK "Windows rejected the PulseFX virtual audio driver (PnPUtil exit code $0). Setup will stop rather than weaken Windows driver-signature or Secure Boot protections. Install a properly signed PulseFX driver package and run setup again."
        Abort
      ${EndIf}

      ; Driver binding and DN_STARTED can appear shortly after PnPUtil returns.
      ; Give Plug and Play a bounded propagation window, but never accept an
      ; unhealthy device merely because staging itself succeeded.
      DetailPrint "Verifying PulseFX virtual audio driver binding..."
      StrCpy $2 0

    pulsefx_health_retry:
      nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" check'
      Pop $0
      Pop $1
      ${If} $0 == 0
        Goto pulsefx_driver_healthy
      ${EndIf}
      IntOp $2 $2 + 1
      ${If} $2 < 20
        Sleep 250
        Goto pulsefx_health_retry
      ${EndIf}

      DetailPrint "Device health output: $1"
      DetailPrint "Removing unhealthy PulseFX virtual device..."
      nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" remove'
      Pop $3
      Pop $4
      MessageBox MB_ICONSTOP|MB_OK "The PulseFX driver package was staged, but the virtual audio device did not bind and start correctly. Setup removed the incomplete device and will stop."
      Abort

    pulsefx_driver_healthy:
      DetailPrint "PulseFX virtual audio device is installed, bound, and started."
      Goto pulsefx_driver_done

    pulsefx_driver_missing:
      nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" remove'
      Pop $2
      Pop $3
      MessageBox MB_ICONSTOP|MB_OK "The PulseFX virtual audio driver package is missing from this installer. Setup cannot provide system-wide processing without it."
      Abort

    pulsefx_helper_missing:
      MessageBox MB_ICONSTOP|MB_OK "The PulseFX virtual-device setup helper is missing from this installer. Setup cannot safely create the system audio endpoint."
      Abort

    pulsefx_driver_done:
  ${endIf}
!macroend

!macro customUnInstall
  ; Per-machine uninstall is elevated by electron-builder's multi-user flow.
  ; Remove the root-enumerated PulseFX devnode before installed resources vanish.
  IfFileExists "$INSTDIR\resources\native\pulsefx_device_setup.exe" pulsefx_uninstall_helper_present pulsefx_uninstall_done

  pulsefx_uninstall_helper_present:
    DetailPrint "Removing PulseFX virtual audio device..."
    nsExec::ExecToStack '"$INSTDIR\resources\native\pulsefx_device_setup.exe" remove'
    Pop $0
    Pop $1
    ${If} $0 != 0
      DetailPrint "PulseFX device removal output: $1"
      MessageBox MB_ICONEXCLAMATION|MB_OK "PulseFX could not fully remove its virtual audio device (exit code $0). Windows may complete device removal after a restart."
    ${EndIf}

  pulsefx_uninstall_done:
!macroend
