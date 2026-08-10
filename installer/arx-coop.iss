; Arx Fatalis Co-op - Windows installer
;
; For the people who would rather not copy files into a game folder by hand.
; The zip is still the real package and always will be; this only finds the
; folder for them, puts the same files in it, and gives them a clean way out.
;
; It installs INTO an existing Arx Fatalis installation, so there is no default
; directory to fall back on - the whole job is finding where the game already
; lives. Four places are tried, in order of how much they can be trusted, and if
; none of them answer the player is asked, with the answer checked before it is
; accepted.
;
; Built by make-installer.sh; version comes in on the command line.

#ifndef Version
  #define Version "0.0"
#endif

[Setup]
AppId={{7A1C2E64-3B5D-4A2F-9C81-ARXCOOP00001}
AppName=Arx Fatalis Co-op
AppVersion={#Version}
AppPublisher=kingzmanh
AppSupportURL=https://github.com/kingzmanh/arx-fatalis-coop
AppUpdatesURL=https://github.com/kingzmanh/arx-fatalis-coop/releases
DefaultDirName={code:FindArx}
DefaultGroupName=Arx Fatalis Co-op
DisableProgramGroupPage=yes
DisableWelcomePage=no
OutputDir=..\release
OutputBaseFilename=arx-coop-{#Version}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; It only ever writes into a folder the player already owns.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayName=Arx Fatalis Co-op {#Version}
DirExistsWarning=no
AppendDefaultDirName=no

[Messages]
WelcomeLabel2=This adds two player co-op to a copy of Arx Fatalis you already own.%n%nIt contains no game content - no art, no sound, no levels. It installs alongside the game's own files and does not replace them: the original arx.exe is left exactly as it is.%n%nBoth players need this same version to play together.%n%nThis mod is free. The only official download is github.com/kingzmanh/arx-fatalis-coop - if you paid for this, you were charged for something given away for free there.
SelectDirLabel3=Setup will add Arx Fatalis Co-op to the game folder below. This must be a folder that already contains Arx Fatalis.
SelectDirBrowseLabel=To continue, click Next. To choose a different folder, click Browse.

[Files]
Source: "..\release\arx-coop-{#Version}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Arx Fatalis Co-op"; Filename: "{app}\arx-coop.exe"; WorkingDir: "{app}"
Name: "{group}\Read me first"; Filename: "{app}\READ ME FIRST.txt"
Name: "{group}\Uninstall Arx Fatalis Co-op"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Arx Fatalis Co-op"; Filename: "{app}\arx-coop.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Run]
Filename: "{app}\arx-coop.exe"; Description: "Play now"; Flags: nowait postinstall skipifsilent
Filename: "{app}\READ ME FIRST.txt"; Description: "Read how to host and join"; Flags: shellexec nowait postinstall skipifsilent unchecked

[Code]

{ A folder is Arx Fatalis if the game's own data is in it. Nothing else is
  proof: the mod's files may be there from a previous install, and an empty
  folder with the right name is a mistake waiting to happen.

  There are two layouts, and the common one is not the obvious one. A retail
  or Steam copy keeps the paks loose in the game folder - data.pak, speech.pak,
  SFX.pak sitting next to arx.exe. A tree set up for Arx Libertatis puts them
  in a data subfolder instead. Checking only for data\data.pak recognised the
  second and rejected every Steam install, which is most of them. }
function IsArxFolder(Path: String): Boolean;
begin
  Result := FileExists(AddBackslash(Path) + 'data.pak')
         or FileExists(AddBackslash(Path) + 'data\data.pak');
end;

{ Steam keeps its libraries in libraryfolders.vdf, which may point at other
  drives. Rather than parse the format properly, every quoted path in it is
  tried - a library folder is only ever a candidate, and IsArxFolder decides. }
function FindInSteam(): String;
var
  SteamPath, Vdf, Line: String;
  Lines: TArrayOfString;
  I, A, B: Integer;
  Candidate: String;
begin
  Result := '';
  if not RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
    exit;
  StringChangeEx(SteamPath, '/', '\', True);

  Candidate := AddBackslash(SteamPath) + 'steamapps\common\Arx Fatalis';
  if IsArxFolder(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  Vdf := AddBackslash(SteamPath) + 'steamapps\libraryfolders.vdf';
  if not LoadStringsFromFile(Vdf, Lines) then
    exit;

  for I := 0 to GetArrayLength(Lines) - 1 do begin
    Line := Lines[I];
    A := Pos('"path"', Line);
    if A > 0 then begin
      Line := Copy(Line, A + 6, Length(Line));
      A := Pos('"', Line);
      if A > 0 then begin
        Line := Copy(Line, A + 1, Length(Line));
        B := Pos('"', Line);
        if B > 1 then begin
          Candidate := Copy(Line, 1, B - 1);
          StringChangeEx(Candidate, '\\', '\', True);
          Candidate := AddBackslash(Candidate) + 'steamapps\common\Arx Fatalis';
          if IsArxFolder(Candidate) then begin
            Result := Candidate;
            exit;
          end;
        end;
      end;
    end;
  end;
end;

function FindInGog(): String;
var
  Path: String;
begin
  Result := '';
  { 1207658680 is Arx Fatalis on GOG. Both views, since the entry is 32 bit. }
  if RegQueryStringValue(HKLM32, 'SOFTWARE\GOG.com\Games\1207658680', 'path', Path)
  or RegQueryStringValue(HKLM64, 'SOFTWARE\GOG.com\Games\1207658680', 'path', Path)
  or RegQueryStringValue(HKCU32, 'SOFTWARE\GOG.com\Games\1207658680', 'path', Path) then
    if IsArxFolder(Path) then
      Result := Path;
end;

{ Arx Libertatis records where it found the data, which is the most reliable
  answer of all: it is not a guess about where a shop puts things, it is a
  folder the engine has actually loaded this game from. }
function FindFromLibertatis(): String;
var
  Path: String;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\ArxLibertatis', 'DataDir', Path)
  or RegQueryStringValue(HKLM, 'Software\ArxLibertatis', 'DataDir', Path) then
    if IsArxFolder(Path) then
      Result := Path;
end;

var
  CachedDir: String;
  CachedDone: Boolean;

function FindArx(Param: String): String;
begin
  if CachedDone then begin
    Result := CachedDir;
    exit;
  end;
  CachedDone := True;

  CachedDir := FindFromLibertatis();
  if CachedDir = '' then CachedDir := FindInSteam();
  if CachedDir = '' then CachedDir := FindInGog();

  { Nothing found: offer somewhere plausible to start browsing from rather than
    an empty box, but the check on the Next button is what actually decides. }
  if CachedDir = '' then
    CachedDir := ExpandConstant('{autopf}\Arx Fatalis');

  Result := CachedDir;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    if not IsArxFolder(WizardDirValue) then begin
      Result := False;
      MsgBox('Arx Fatalis does not seem to be in that folder.' + #13#10#13#10 +
             'Setup looked for data.pak in it, and in a data subfolder, and ' +
             'found neither.' + #13#10#13#10 +
             'Choose the folder the game itself is installed in - the one with ' +
             'arx.exe and the .pak files in it. This mod needs the real game to ' +
             'run: it contains no game content of its own.',
             mbError, MB_OK);
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then begin
    if IsArxFolder(WizardDirValue) then
      WizardForm.DirEdit.Hint := 'Arx Fatalis found here.';
  end;
end;
