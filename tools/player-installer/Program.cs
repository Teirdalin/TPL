using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

namespace TPLInstaller {
    internal sealed class Payload {
        internal readonly string Resource,RelativePath;
        internal readonly bool Preserve;
        internal Payload(string resource,string relativePath,bool preserve) { Resource=resource; RelativePath=relativePath; Preserve=preserve; }
    }
    internal sealed class InstallResult { internal string GameDirectory,ConfigurationBackup; }

    internal static class InstallerCore {
        internal static readonly Payload[] Files=new Payload[] {
            new Payload("TPL.Payload.0","TPL.dll",false),
            new Payload("TPL.Payload.1","TPL\\versions\\0.1.5\\TPL.Runtime.dll",false),
            new Payload("TPL.Payload.2","TPL\\versions\\0.1.5\\TPL.Update.ps1",false),
            new Payload("TPL.Payload.3","TPL\\versions\\0.1.5\\runtime.json",false),
            new Payload("TPL.Payload.4","TPL\\current.txt",true),
            new Payload("TPL.Payload.5","TPL\\automatic-updates.txt",true),
            new Payload("TPL.Payload.6","TPL\\README.md",false),
            new Payload("TPL.Payload.7","TPL\\LICENSE.txt",false),
            new Payload("TPL.Payload.8","TPL\\PLUGIN_API_PERMISSION.md",false),
            new Payload("TPL.Payload.9","TPL\\THIRD_PARTY_NOTICES.md",false),
            new Payload("TPL.Payload.10","TPL\\Licenses\\JDL-1.txt",false),
            new Payload("TPL.Payload.11","TPL\\Licenses\\MyGUI.LICENSE.txt",false),
            new Payload("TPL.Payload.12","TPL\\Licenses\\RapidJSON.LICENSE.txt",false),
            new Payload("TPL.Payload.13","TPL\\Licenses\\MinHook.LICENSE.txt",false)
        };

        internal static bool IsKenshiDirectory(string value) {
            if(String.IsNullOrWhiteSpace(value)) return false;
            try { string path=Path.GetFullPath(value); return File.Exists(Path.Combine(path,"kenshi_x64.exe")) && File.Exists(Path.Combine(path,"Plugins_x64.cfg")); }
            catch { return false; }
        }
        internal static bool IsInstalled(string game) {
            if(!IsKenshiDirectory(game)) return false;
            try { return File.Exists(Path.Combine(game,"TPL.dll")) || Regex.IsMatch(File.ReadAllText(Path.Combine(game,"Plugins_x64.cfg")),@"(?mi)^\s*Plugin\s*=\s*TPL\s*$"); }
            catch { return false; }
        }
        internal static bool KenshiRunning() { return Process.GetProcessesByName("kenshi_x64").Length!=0 || Process.GetProcessesByName("kenshi_GOG_x64").Length!=0; }
        internal static bool CanWrite(string directory) {
            string test=null;
            try { test=Path.Combine(directory,".tpl-write-test-"+Guid.NewGuid().ToString("N")); File.WriteAllBytes(test,new byte[] {84}); File.Delete(test); return true; }
            catch { if(test!=null) try { File.Delete(test); } catch {} return false; }
        }

        internal static IList<string> FindKenshiDirectories() {
            List<string> found=new List<string>();
            Add(found,AppDomain.CurrentDomain.BaseDirectory);
            Add(found,ReadRegistry(RegistryHive.LocalMachine,RegistryView.Registry32,@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 233860","InstallLocation"));
            string steam=ReadRegistry(RegistryHive.CurrentUser,RegistryView.Default,@"Software\Valve\Steam","SteamPath");
            if(String.IsNullOrEmpty(steam)) steam=ReadRegistry(RegistryHive.LocalMachine,RegistryView.Registry32,@"SOFTWARE\Valve\Steam","InstallPath");
            AddSteam(found,steam);
            try {
                foreach(DriveInfo drive in DriveInfo.GetDrives()) if(drive.IsReady && drive.DriveType==DriveType.Fixed) {
                    Add(found,Path.Combine(drive.RootDirectory.FullName,@"SteamLibrary\steamapps\common\Kenshi"));
                    Add(found,Path.Combine(drive.RootDirectory.FullName,@"Steam\steamapps\common\Kenshi"));
                }
            } catch {}
            return found;
        }
        private static string ReadRegistry(RegistryHive hive,RegistryView view,string path,string name) {
            try { using(RegistryKey baseKey=RegistryKey.OpenBaseKey(hive,view)) using(RegistryKey key=baseKey.OpenSubKey(path)) return key==null?null:key.GetValue(name) as string; }
            catch { return null; }
        }
        private static void AddSteam(List<string> found,string steam) {
            if(String.IsNullOrEmpty(steam)) return;
            Add(found,Path.Combine(steam,@"steamapps\common\Kenshi"));
            string vdf=Path.Combine(steam,@"steamapps\libraryfolders.vdf");
            try {
                if(!File.Exists(vdf)) return;
                foreach(Match match in Regex.Matches(File.ReadAllText(vdf),@"""path""\s+""([^""]+)"""))
                    Add(found,Path.Combine(match.Groups[1].Value.Replace("\\\\","\\"),@"steamapps\common\Kenshi"));
            } catch {}
        }
        private static void Add(List<string> found,string candidate) {
            if(!IsKenshiDirectory(candidate)) return;
            string full=Path.GetFullPath(candidate).TrimEnd(Path.DirectorySeparatorChar);
            foreach(string current in found) if(String.Equals(current,full,StringComparison.OrdinalIgnoreCase)) return;
            found.Add(full);
        }

        internal static InstallResult Install(string gameDirectory) {
            Validate(gameDirectory,"installing");
            string game=Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
            Dictionary<string,byte[]> data=LoadResources();
            string stamp=DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");
            string cfg=Path.Combine(game,"Plugins_x64.cfg"), cfgBackup=cfg+".tpl-"+stamp+".bak";
            File.Copy(cfg,cfgBackup,false);
            string bootstrap=Path.Combine(game,"TPL.dll");
            if(File.Exists(bootstrap)) File.Copy(bootstrap,bootstrap+"."+stamp+".bak",false);
            string version=Path.Combine(game,@"TPL\versions\0.1.5");
            if(Directory.Exists(version) && VersionChanges(game,data)) CopyDirectory(version,version+".backup-"+stamp);
            foreach(Payload payload in Files) {
                string destination=Contained(game,payload.RelativePath);
                if(payload.Preserve && File.Exists(destination)) continue;
                AtomicWrite(destination,data[payload.Resource]);
                if(!EqualBytes(destination,data[payload.Resource])) throw new IOException("Installed file checksum mismatch: "+payload.RelativePath);
            }
            foreach(string marker in new string[]{"current.txt","pending.txt","attempt.txt"}) {
                string path=Path.Combine(game,"TPL\\"+marker);
                if(File.Exists(path)) File.Copy(path,path+".tpl-"+stamp+".bak",false);
            }
            AtomicWrite(Path.Combine(game,@"TPL\current.txt"),Encoding.UTF8.GetBytes("0.1.5"));
            foreach(string marker in new string[]{"pending.txt","attempt.txt"}) {
                string path=Path.Combine(game,"TPL\\"+marker);
                if(File.Exists(path)) File.Delete(path);
            }
            AtomicWrite(cfg,UpdateConfiguration(File.ReadAllText(cfg),true));
            InstallResult result=new InstallResult(); result.GameDirectory=game; result.ConfigurationBackup=cfgBackup; return result;
        }

        internal static InstallResult Uninstall(string gameDirectory) {
            Validate(gameDirectory,"uninstalling");
            string game=Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
            string stamp=DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");
            string cfg=Path.Combine(game,"Plugins_x64.cfg"), cfgBackup=cfg+".tpl-uninstall-"+stamp+".bak";
            File.Copy(cfg,cfgBackup,false);
            AtomicWrite(cfg,UpdateConfiguration(File.ReadAllText(cfg),false));
            string bootstrap=Path.Combine(game,"TPL.dll");
            if(File.Exists(bootstrap)) File.Move(bootstrap,bootstrap+".tpl-uninstalled-"+stamp+".bak");
            string version=Path.Combine(game,@"TPL\versions\0.1.5");
            if(Directory.Exists(version)) Directory.Move(version,version+".uninstalled-"+stamp);
            InstallResult result=new InstallResult(); result.GameDirectory=game; result.ConfigurationBackup=cfgBackup; return result;
        }

        private static void Validate(string game,string operation) {
            if(KenshiRunning()) throw new InvalidOperationException("Close Kenshi before "+operation+" TPL.");
            if(!IsKenshiDirectory(game)) throw new InvalidOperationException("Select the folder containing kenshi_x64.exe and Plugins_x64.cfg.");
        }
        private static Dictionary<string,byte[]> LoadResources() {
            Dictionary<string,byte[]> data=new Dictionary<string,byte[]>(); Assembly assembly=Assembly.GetExecutingAssembly();
            foreach(Payload payload in Files) using(Stream stream=assembly.GetManifestResourceStream(payload.Resource)) {
                if(stream==null) throw new InvalidOperationException("Installer payload is incomplete: "+payload.RelativePath);
                using(MemoryStream memory=new MemoryStream()) { stream.CopyTo(memory); data[payload.Resource]=memory.ToArray(); }
            }
            return data;
        }
        private static string Contained(string root,string relative) {
            string path=Path.GetFullPath(Path.Combine(root,relative));
            if(!path.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Installer path escaped the Kenshi folder.");
            return path;
        }
        private static bool VersionChanges(string game,Dictionary<string,byte[]> data) {
            for(int i=1;i<=3;i++) { string path=Contained(game,Files[i].RelativePath); if(!File.Exists(path) || !EqualBytes(path,data[Files[i].Resource])) return true; }
            return false;
        }
        private static bool EqualBytes(string path,byte[] expected) {
            FileInfo info=new FileInfo(path); if(!info.Exists || info.Length!=expected.Length) return false;
            byte[] actual=new byte[8192]; int offset=0;
            using(FileStream stream=File.OpenRead(path)) while(offset<expected.Length) {
                int got=stream.Read(actual,0,Math.Min(actual.Length,expected.Length-offset)); if(got<=0) return false;
                for(int i=0;i<got;i++) if(actual[i]!=expected[offset+i]) return false;
                offset+=got;
            }
            return true;
        }
        private static void CopyDirectory(string source,string destination) {
            Directory.CreateDirectory(destination);
            foreach(string file in Directory.GetFiles(source)) File.Copy(file,Path.Combine(destination,Path.GetFileName(file)),false);
            foreach(string folder in Directory.GetDirectories(source)) {
                if((File.GetAttributes(folder)&FileAttributes.ReparsePoint)!=0) throw new IOException("Refusing a redirected folder in the existing TPL version.");
                CopyDirectory(folder,Path.Combine(destination,Path.GetFileName(folder)));
            }
        }
        private static void AtomicWrite(string path,byte[] bytes) {
            string directory=Path.GetDirectoryName(path); Directory.CreateDirectory(directory);
            string temporary=path+".tpl-install-"+Guid.NewGuid().ToString("N")+".tmp";
            try { File.WriteAllBytes(temporary,bytes); if(File.Exists(path)) File.Replace(temporary,path,null,true); else File.Move(temporary,path); }
            finally { try { if(File.Exists(temporary)) File.Delete(temporary); } catch {} }
        }
        private static void AtomicWrite(string path,string text) { AtomicWrite(path,new UTF8Encoding(false).GetBytes(text)); }
        private static string UpdateConfiguration(string text,bool install) {
            string[] split=Regex.Split(text,"\\r?\\n"); List<string> lines=new List<string>();
            foreach(string line in split) if(!Regex.IsMatch(line,@"^\s*Plugin\s*=\s*TPL\s*$",RegexOptions.IgnoreCase)) lines.Add(line);
            if(!install) return String.Join("\r\n",lines.ToArray());
            bool preferred=false; foreach(string line in lines) if(Regex.IsMatch(line,@"^\s*Plugin\s*=\s*RE_Kenshi\s*$",RegexOptions.IgnoreCase)) preferred=true;
            List<string> result=new List<string>(); bool inserted=false;
            foreach(string line in lines) {
                if(!preferred && !inserted && Regex.IsMatch(line,@"^\s*Plugin\s*=",RegexOptions.IgnoreCase)) { result.Add("Plugin=TPL"); inserted=true; }
                result.Add(line);
                if(preferred && Regex.IsMatch(line,@"^\s*Plugin\s*=\s*RE_Kenshi\s*$",RegexOptions.IgnoreCase)) { result.Add("Plugin=TPL"); inserted=true; }
            }
            if(!inserted) result.Add("Plugin=TPL"); return String.Join("\r\n",result.ToArray());
        }
    }

    internal sealed class InstallerForm : Form {
        private readonly TextBox path=new TextBox();
        private readonly Button install=new Button(),uninstall=new Button();
        private readonly Label status=new Label();
        internal InstallerForm() {
            Text="Teirdalin's Plugin Loader Installer"; ClientSize=new Size(650,344); FormBorderStyle=FormBorderStyle.FixedDialog; MaximizeBox=false;
            StartPosition=FormStartPosition.CenterScreen; Font=new Font("Segoe UI",9F);
            Label title=new Label(); title.Text="Teirdalin's Plugin Loader"; title.Font=new Font("Segoe UI Semibold",18F); title.SetBounds(24,20,590,42); Controls.Add(title);
            Label description=new Label(); description.Text="Installs TPL and adds it to Kenshi's native plugin configuration. Existing mods, settings and saves are preserved. Close Kenshi before continuing."; description.SetBounds(27,69,592,52); Controls.Add(description);
            Label pathLabel=new Label(); pathLabel.Text="Kenshi folder"; pathLabel.SetBounds(27,132,150,22); Controls.Add(pathLabel);
            path.SetBounds(27,155,500,25); path.TextChanged+=delegate { RefreshState(); }; Controls.Add(path);
            Button browse=new Button(); browse.Text="Browse..."; browse.SetBounds(535,153,86,29); browse.Click+=Browse; Controls.Add(browse);
            Label notice=new Label(); notice.Text="Backups are created automatically. Uninstall keeps plugins, settings, saves, and recovery backups for a future reinstall."; notice.SetBounds(27,197,592,40); Controls.Add(notice);
            status.SetBounds(27,245,380,45); status.ForeColor=Color.FromArgb(90,90,90); Controls.Add(status);
            uninstall.Text="Uninstall TPL"; uninstall.SetBounds(373,274,120,38); uninstall.Click+=UninstallClicked; Controls.Add(uninstall);
            install.Text="Install TPL"; install.SetBounds(501,274,120,38); install.Click+=InstallClicked; Controls.Add(install); AcceptButton=install;
            IList<string> candidates=InstallerCore.FindKenshiDirectories(); if(candidates.Count>0) path.Text=candidates[0]; else RefreshState();
        }
        private void RefreshState() {
            bool valid=InstallerCore.IsKenshiDirectory(path.Text); bool present=valid && InstallerCore.IsInstalled(path.Text);
            install.Enabled=valid; uninstall.Enabled=present; install.Text=present?"Reinstall TPL":"Install TPL";
            status.Text=!valid?"Choose the Kenshi folder containing kenshi_x64.exe.":present?"TPL is installed. Reinstall or uninstall it below.":"Kenshi found. Ready to install TPL.";
        }
        private void Browse(object sender,EventArgs e) {
            using(FolderBrowserDialog dialog=new FolderBrowserDialog()) {
                dialog.Description="Select the Kenshi folder containing kenshi_x64.exe"; dialog.ShowNewFolderButton=false;
                if(Directory.Exists(path.Text)) dialog.SelectedPath=path.Text;
                if(dialog.ShowDialog(this)==DialogResult.OK) path.Text=dialog.SelectedPath;
            }
        }
        private bool Ready(string action) {
            if(!InstallerCore.IsKenshiDirectory(path.Text)) { MessageBox.Show(this,"Select the folder containing kenshi_x64.exe and Plugins_x64.cfg.","Kenshi folder not found",MessageBoxButtons.OK,MessageBoxIcon.Warning); return false; }
            if(InstallerCore.KenshiRunning()) { MessageBox.Show(this,"Close Kenshi before "+action+" TPL.","Kenshi is running",MessageBoxButtons.OK,MessageBoxIcon.Warning); return false; }
            return true;
        }
        private void InstallClicked(object sender,EventArgs e) {
            if(!Ready("installing")) return; if(!InstallerCore.CanWrite(path.Text)) { RelaunchElevated("install"); return; }
            Run(false);
        }
        private void UninstallClicked(object sender,EventArgs e) {
            if(!Ready("uninstalling")) return;
            if(MessageBox.Show(this,"Uninstall TPL from Kenshi?\r\n\r\nPlugins, settings, saves, and recovery backups will be retained.","Uninstall TPL",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes) return;
            if(!InstallerCore.CanWrite(path.Text)) { RelaunchElevated("uninstall"); return; }
            Run(true);
        }
        private void Run(bool removing) {
            install.Enabled=false; uninstall.Enabled=false; status.Text=removing?"Uninstalling...":"Installing..."; Refresh();
            try {
                InstallResult result=removing?InstallerCore.Uninstall(path.Text):InstallerCore.Install(path.Text);
                string message=removing?"TPL was removed from startup. Plugins and settings were retained.":"TPL is installed and ready.";
                MessageBox.Show(this,message+"\r\n\r\nConfiguration backup:\r\n"+result.ConfigurationBackup,removing?"Uninstall complete":"Installation complete",MessageBoxButtons.OK,MessageBoxIcon.Information);
                RefreshState();
            } catch(Exception ex) { MessageBox.Show(this,ex.Message,"TPL setup failed",MessageBoxButtons.OK,MessageBoxIcon.Error); RefreshState(); }
        }
        private void RelaunchElevated(string action) {
            try {
                ProcessStartInfo info=new ProcessStartInfo(Application.ExecutablePath); info.UseShellExecute=true; info.Verb="runas";
                info.Arguments="--elevated-"+action+" --game-dir \""+path.Text.Replace("\"","\\\"")+"\""; Process.Start(info); Close();
            } catch(Exception ex) { MessageBox.Show(this,"Administrator permission is needed for this Kenshi folder.\r\n\r\n"+ex.Message,"Permission required",MessageBoxButtons.OK,MessageBoxIcon.Warning); }
        }
    }

    internal static class Program {
        [STAThread]
        private static int Main(string[] args) {
            bool silent=false,remove=false,elevated=false; string game=null;
            for(int i=0;i<args.Length;i++) {
                if(args[i]=="--silent") silent=true; else if(args[i]=="--uninstall") remove=true;
                else if(args[i]=="--elevated-install") elevated=true; else if(args[i]=="--elevated-uninstall") { elevated=true; remove=true; }
                else if(args[i]=="--game-dir" && i+1<args.Length) game=args[++i];
            }
            if(silent || elevated) {
                try {
                    InstallResult result=remove?InstallerCore.Uninstall(game):InstallerCore.Install(game);
                    if(elevated) MessageBox.Show((remove?"TPL was uninstalled.":"TPL is installed and ready.")+"\r\n\r\nBackup: "+result.ConfigurationBackup,"TPL setup complete",MessageBoxButtons.OK,MessageBoxIcon.Information);
                    return 0;
                } catch(Exception ex) { if(elevated) MessageBox.Show(ex.Message,"TPL setup failed",MessageBoxButtons.OK,MessageBoxIcon.Error); return 1; }
            }
            Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false); Application.Run(new InstallerForm()); return 0;
        }
    }
}
