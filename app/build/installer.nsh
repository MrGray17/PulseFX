!macro customInstall
  ${ifNot} ${isUpdated}
    IfFileExists "$INSTDIR\resources\driver\*.inf" pulsefx_driver_present pulsefx_driver_missing

    pulsefx_driver_present:
      DetailPrint "Installing PulseFX virtual audio driver..."
      nsExec::ExecToStack '"$SYSDIR\pnputil.exe" /add-driver "$INSTDIR\resources\driver\*.inf" /subdirs /install'
      Pop $0
      Pop $1
      ${If} $0 != 0
        DetailPrint "PnPUtil output: $1"
        MessageBox MB_ICONSTOP|MB_OK "Windows rejected the PulseFX virtual audio driver (PnPUtil exit code $0). Setup will stop rather than weaken Windows driver-signature or Secure Boot protections. Install a properly signed PulseFX driver package and run setup again."
        Abort
      ${EndIf}
      DetailPrint "PulseFX virtual audio driver installed."
      Goto pulsefx_driver_done

    pulsefx_driver_missing:
      MessageBox MB_ICONSTOP|MB_OK "The PulseFX virtual audio driver package is missing from this installer. Setup cannot provide system-wide processing without it."
      Abort

    pulsefx_driver_done:
  ${endIf}
!macroend
