# KrellMail on Windows

Install the signed ZIP for your Windows Krellix preview, extract it, and run
`./install-krellmail.ps1 -KrellixDirectory C:/path/to/Krellix` in PowerShell.
Reopen the **local** window and configure accounts in Settings / Plugins.
The installer does not copy accounts or turn on the plugin automatically.

KrellMail 0.1.5-windows.1 supports the Windows preview.7 host. Choose the
mail animation under Settings / Plugins / Krellmail.
Install Bluemask separately and select it in Appearance to use its mail artwork.
Choose the mail panel's `...` menu to set the unread animation to Follow Theme,
Envelope, or Envelope with Tux. The choice is saved per Krellix profile.

Set up IMAP or POP3 with TLS enabled. KrellMail uses Qt's certificate
validation and the packaged Windows TLS backend. Remote windows do not
check this machine's accounts; the remote daemon does not supply mailbox data.

`ctest -R krellmail-integration --output-on-failure` checks DLL loading,
loopback IMAP and POP counts, error handling, active-check shutdown and remote
isolation. It checks for an available TLS backend but does not authenticate to
a real provider. No customer mail credentials are included in the package or
used by the test. The `krellmail-icon` test covers themed icons and animation.
