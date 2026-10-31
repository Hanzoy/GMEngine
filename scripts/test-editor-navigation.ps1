param([string]$Bin, [switch]$InputEvents)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
if(-not $Bin){$Bin=Join-Path $engineBuild 'Release'}
$testRoot=Join-Path $engineBuildRoot ('navigation-qa/'+[guid]::NewGuid().ToString('N'))
$session=Join-Path $testRoot 'live'
New-Item -ItemType Directory -Force $testRoot|Out-Null
$scenePath=Join-Path $testRoot 'navigation.scene.json'
function Transform($x,$y,$z){return @{type='Transform3D';version=1;fields=@{x=$x;y=$y;z=$z;rotationX=0;rotationY=0;rotationZ=0;scaleX=1;scaleY=1;scaleZ=1}}}
function Mesh {return @{type='MeshRenderer';version=1;fields=@{mesh='builtin:cube';material='';enabled=$true}}}
$source=@{format='gm.scene';version=1;name='Navigation regression';width=1280;height=720;background='#18212D';entities=@(
    @{id='group';name='Group';parent=$null;components=@()},
    @{id='near';name='Near cube';parent='group';components=@((Transform 0 0 0),(Mesh))},
    @{id='far';name='Far cube';parent='group';components=@((Transform 0 0 5),(Mesh))},
    @{id='empty';name='Empty';parent='group';components=@((Transform 4 2 0))}
)}
$source|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $scenePath -Encoding utf8
$originalHash=(Get-FileHash $scenePath).Hash
$editor=Join-Path $Bin 'gm_editor.exe';$cli=Join-Path $Bin 'gmctl.exe'
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class NavigationInput {
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr w,ref Point p);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,int x,int y,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
 [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w,out uint process);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int cx,int cy,uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr w,int command);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
 public static IntPtr Window(int process){IntPtr w=IntPtr.Zero;while((w=FindWindowEx(IntPtr.Zero,w,"GMEngineEditor",null))!=IntPtr.Zero){uint id;GetWindowThreadProcessId(w,out id);if(id==process)return w;}return IntPtr.Zero;}
 public static void Key(byte k,bool down){keybd_event(k,0,down?0u:2u,UIntPtr.Zero);}
 public static void Mouse(uint flags,int x=0,int y=0,uint wheel=0){mouse_event(flags,x,y,wheel,UIntPtr.Zero);}
}
'@
$checks=[System.Collections.Generic.List[string]]::new()
function Check($value,$message){if(-not $value){throw "FAILED: $message"};$checks.Add($message)}
function Send($request){$result=$request|ConvertTo-Json -Depth 20 -Compress|& $cli --live $session|Out-String|ConvertFrom-Json;if(-not $result.ok){throw ($result|ConvertTo-Json -Depth 10)};return $result}
function Camera {return (Send @{op='view.camera'}).result}
function StartEditor {
    $script:process=Start-Process -FilePath $editor -ArgumentList @('--project',('"'+$scenePath+'"'),'--session',('"'+$session+'"')) -WindowStyle Hidden -PassThru
    for($i=0;$i -lt 100;$i++){if($process.HasExited){throw 'Editor exited'};if(Test-Path "$session/session.json"){$info=Get-Content "$session/session.json" -Raw|ConvertFrom-Json;if($info.pid -eq $process.Id){break}};Start-Sleep -Milliseconds 100}
    if(-not(Test-Path "$session/session.json")){throw 'Editor startup timed out'}
    for($i=0;$i -lt 40;$i++){$script:view=Camera;if($view.viewport[2] -gt 0){break};Start-Sleep -Milliseconds 50}
}
function CloseEditor {if($process -and -not $process.HasExited){$null=[NavigationInput]::PostMessage([NavigationInput]::Window($process.Id),16,[IntPtr]::Zero,[IntPtr]::Zero);if(-not $process.WaitForExit(5000)){Stop-Process -Id $process.Id;Write-Warning 'Editor failed to close gracefully'}}}
try {
    StartEditor
    $initial=Send @{op='query'}
    Check ($view.viewport[2] -gt 0) '3D Scene view works without Camera3D'
    $null=Send @{op='view.camera';set=@{position=@(0,0,-5);yaw=0;pitch=0;distance=5;speed=5}}
    $picked=Send @{op='view.pick';x=.5;y=.5}
    Check ($picked.result.selected -eq 'near') 'Triangle picking selects the nearest visible mesh'
    $null=Send @{op='view.navigate';action='orbit';x=100;y=40}
    $orbit=Camera
    Check ($orbit.yaw -eq 20 -and $orbit.pitch -eq 8) 'Live orbit uses shared camera controller'
    $null=Send @{op='select';id='group'};$null=Send @{op='view.frame'}
    Check ((Camera).distance -gt 5) 'Frame selection includes child meshes'
    $null=Send @{op='select';id='empty'};$null=Send @{op='view.frame'}
    Check ((Camera).distance -gt 0) 'Frame an empty entity beneath a transformless parent'
    $before=Camera
    $bad=@{op='view.camera';set=@{pitch=100}}|ConvertTo-Json -Compress|& $cli --live $session|Out-String|ConvertFrom-Json
    Check (-not $bad.ok -and (Camera).pitch -eq $before.pitch) 'Invalid camera update is atomic'
    $null=Send @{op='view.navigate';action='pan';x=30;y=20};$null=Send @{op='view.navigate';action='dolly';steps=2}
    $null=Send @{op='view.navigate';action='fly';x=1;y=0;z=1;seconds=.1;fast=$true}
    $null=Send @{op='preview.run';paused=$true}
    $gameBefore=(Send @{op='runtime.query'}).result|ConvertTo-Json -Depth 30 -Compress
    $null=Send @{op='view.scene';game=$false};$null=Send @{op='view.navigate';action='look';x=40;y=-20}
    $gameAfter=(Send @{op='runtime.query'}).result|ConvertTo-Json -Depth 30 -Compress
    Check ($gameBefore -eq $gameAfter) 'Navigating during paused Play does not change game runtime'
    $null=Send @{op='view.scene';game=$true};$null=Send @{op='preview.stop'}
    $after=Send @{op='query'}
    Check ($after.revision -eq $initial.revision -and $after.undo -eq $initial.undo -and -not $after.dirty) 'Navigation leaves revision, undo and dirty state unchanged'
    if($InputEvents) {

        $null=[NavigationInput]::SetThreadDpiAwarenessContext([IntPtr](-4))
        $window=[NavigationInput]::Window($process.Id);$null=[NavigationInput]::ShowWindow($window,9)
        $null=[NavigationInput]::SetWindowPos($window,[IntPtr]::Zero,40,40,0,0,1)
        [NavigationInput]::Key(18,$true);$null=[NavigationInput]::SetForegroundWindow($window);[NavigationInput]::Key(18,$false)
        Start-Sleep -Milliseconds 150
        Check ([NavigationInput]::GetForegroundWindow() -eq $window) 'Native input test window is foreground' 
        $view=Camera;$point=[NavigationInput+Point]::new();$point.x=[int]($view.viewport[0]+$view.viewport[2]/2);$point.y=[int]($view.viewport[1]+$view.viewport[3]/2)
        $null=[NavigationInput]::ClientToScreen($window,[ref]$point);$null=[NavigationInput]::SetCursorPos($point.x,$point.y)
        Start-Sleep -Milliseconds 150
        $before=Camera;[NavigationInput]::Mouse(8);Start-Sleep -Milliseconds 100
        [NavigationInput]::Mouse(1,80,-20);Start-Sleep -Milliseconds 150
        $turned=Camera;if($turned.gesture -ne "fly"){Write-Host ("Input trace: "+($before|ConvertTo-Json -Compress)+" -> "+($turned|ConvertTo-Json -Compress))};Check ($turned.gesture -eq 'fly' -and $turned.yaw -ne $before.yaw) 'Right mouse raw input turns the scene camera'
        [NavigationInput]::Key(87,$true);Start-Sleep -Milliseconds 300;[NavigationInput]::Key(87,$false)
        $moved=Camera;Check (($moved.position -join ',') -ne ($turned.position -join ',')) 'Held W moves continuously while right mouse is down'
        [NavigationInput]::Mouse(0x800,0,0,120);Start-Sleep -Milliseconds 100
        Check ((Camera).speed -gt $moved.speed) 'Fly scroll wheel adjusts speed'
        [NavigationInput]::Mouse(16);Start-Sleep -Milliseconds 100
        Check ((Camera).gesture -eq '') 'Right release ends capture'
        $before=Camera;[NavigationInput]::Mouse(0x20);Start-Sleep -Milliseconds 80;[NavigationInput]::Mouse(1,40,25);Start-Sleep -Milliseconds 80;[NavigationInput]::Mouse(0x40)
        Check (((Camera).position -join ',') -ne ($before.position -join ',')) 'Middle mouse pans'
        [NavigationInput]::Key(18,$true);[NavigationInput]::Mouse(2);Start-Sleep -Milliseconds 100
        Check ((Camera).gesture -eq 'orbit') 'Alt plus left mouse enters orbit without selecting'
        [NavigationInput]::Mouse(1,50,10);Start-Sleep -Milliseconds 80;[NavigationInput]::Mouse(4);[NavigationInput]::Key(18,$false);Start-Sleep -Milliseconds 100
        [NavigationInput]::Key(18,$true);[NavigationInput]::Mouse(8);Start-Sleep -Milliseconds 100
        Check ((Camera).gesture -eq 'dolly') 'Alt plus right mouse dollies'
        [NavigationInput]::Mouse(1,30,-10);Start-Sleep -Milliseconds 80;[NavigationInput]::Mouse(16);[NavigationInput]::Key(18,$false);Start-Sleep -Milliseconds 100
        [NavigationInput]::Key(18,$true);[NavigationInput]::Key(17,$true);[NavigationInput]::Mouse(2);Start-Sleep -Milliseconds 100
        Check ((Camera).gesture -eq 'pan') 'Alt Ctrl left mouse pans'
        [NavigationInput]::Mouse(4);[NavigationInput]::Key(17,$false);[NavigationInput]::Key(18,$false);Start-Sleep -Milliseconds 100
        $before=Camera;[NavigationInput]::Key(113,$true);[NavigationInput]::Key(113,$false);Start-Sleep -Milliseconds 80
        [NavigationInput]::Key(70,$true);[NavigationInput]::Key(70,$false);Start-Sleep -Milliseconds 80
        Check (((Camera).position -join ',') -eq ($before.position -join ',')) 'F in a text field does not frame the scene'
        [NavigationInput]::Key(27,$true);[NavigationInput]::Key(27,$false);Start-Sleep -Milliseconds 80
        [NavigationInput]::Key(27,$true);[NavigationInput]::Key(27,$false);Start-Sleep -Milliseconds 80
        [NavigationInput]::Key(70,$true);[NavigationInput]::Key(70,$false);Start-Sleep -Milliseconds 80
        Check (((Camera).position -join ',') -ne ($before.position -join ',')) 'F after leaving text edit frames selection'
        [NavigationInput]::Mouse(8);Start-Sleep -Milliseconds 80;[NavigationInput]::Key(27,$true);[NavigationInput]::Key(27,$false);Start-Sleep -Milliseconds 80
        $esc=Camera;Check ($esc.gesture -eq '') 'Escape cancels capture';[NavigationInput]::Mouse(16)
        [NavigationInput]::Mouse(8);Start-Sleep -Milliseconds 80
        $null=[NavigationInput]::SendMessage($window,8,[IntPtr]::Zero,[IntPtr]::Zero)
        Check ((Camera).gesture -eq '') 'Focus loss cancels navigation';[NavigationInput]::Mouse(16)
        $after=Send @{op='query'}
        Check (-not $after.dirty -and $after.revision -eq 0) 'Native mouse and keyboard gestures do not modify scene'
    }
    $persist=Camera
    CloseEditor;StartEditor
    $restored=Camera
    Check (($restored.position -join ',') -eq ($persist.position -join ',') -and $restored.yaw -eq $persist.yaw -and $restored.speed -eq $persist.speed) 'Per-scene camera survives editor restart'
    Check ((Get-FileHash $scenePath).Hash -eq $originalHash) 'Scene source file is byte-for-byte unchanged'
    $report=@{passed=$checks.Count;checks=$checks;fixture=$testRoot;inputEvents=[bool]$InputEvents}
    $report|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $engineBuild 'navigation-verification.json') -Encoding utf8
    $report|ConvertTo-Json -Depth 10
} finally {
    if($InputEvents -and ('NavigationInput' -as [type])){foreach($key in 17,18,87,27,70,113){[NavigationInput]::Key($key,$false)};[NavigationInput]::Mouse(16);[NavigationInput]::Mouse(4);[NavigationInput]::Mouse(0x40)}
    CloseEditor
}
