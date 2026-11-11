; Instalador do Pauta para Windows (Inno Setup 6).
; Compilar: ISCC /DAppVersion=0.3.0 packaging\windows\pauta.iss
; Entrada: build\dist (pasta portátil gerada por "build.cmd dist" ou pelo CI).
; Saída:   build\installer\Pauta.exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6F3B2A41-7C9D-4E58-9A1B-5D2C8E7F0A13}
AppName=Pauta
AppVersion={#AppVersion}
AppVerName=Pauta {#AppVersion}
AppPublisher=Projeto Pauta
DefaultDirName={autopf}\Pauta
DefaultGroupName=Pauta
DisableProgramGroupPage=yes
; Instala sem pedir senha de administrador (só para o usuário atual);
; quem quiser pode escolher instalar para todos.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\build\installer
OutputBaseFilename=Pauta
SetupIconFile=..\..\resources\pauta.ico
UninstallDisplayIcon={app}\Pauta.exe
UninstallDisplayName=Pauta
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "assoc"; Description: "Abrir arquivos .pauta com o Pauta"; GroupDescription: "Associação de arquivos:"

[Files]
Source: "..\..\build\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Pauta"; Filename: "{app}\Pauta.exe"
Name: "{autodesktop}\Pauta"; Filename: "{app}\Pauta.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.pauta"; ValueType: string; ValueName: ""; ValueData: "Pauta.Partitura"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Pauta.Partitura"; ValueType: string; ValueName: ""; ValueData: "Partitura do Pauta"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Pauta.Partitura\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Pauta.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Pauta.Partitura\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pauta.exe"" ""%1"""; Tasks: assoc
; MusicXML: o Pauta aparece em "Abrir com", sem virar o programa padrão.
Root: HKA; Subkey: "Software\Classes\.musicxml\OpenWithProgids"; ValueType: string; ValueName: "Pauta.Partitura"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.mxl\OpenWithProgids"; ValueType: string; ValueName: "Pauta.Partitura"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Pauta.Partitura\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pauta.exe"" ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\Pauta.exe"; Description: "{cm:LaunchProgram,Pauta}"; Flags: nowait postinstall skipifsilent
