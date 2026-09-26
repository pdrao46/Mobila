; Instalador do MOBILADOR — Android Gaming Bridge (Inno Setup 6)
#ifndef BuildDir
  #define BuildDir "..\build\Release"
#endif
#define AppName "MOBILADOR"
#define AppVersion "1.0.0"

[Setup]
AppId={{6E3C1B8A-4E5F-4B1C-9A8E-0D1A2B3C4D5E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} — Android Gaming Bridge
AppPublisher=Mobilador
DefaultDirName={autopf}\Mobilador
DefaultGroupName=Mobilador
UninstallDisplayIcon={app}\Mobilador.exe
SetupIconFile=mobilador.ico
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputBaseFilename=Mobilador-Setup-{#AppVersion}
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na Área de Trabalho"; GroupDescription: "Atalhos:"

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\MOBILADOR"; Filename: "{app}\Mobilador.exe"
Name: "{autodesktop}\MOBILADOR"; Filename: "{app}\Mobilador.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Mobilador.exe"; Description: "Iniciar o MOBILADOR"; Flags: nowait postinstall skipifsilent
