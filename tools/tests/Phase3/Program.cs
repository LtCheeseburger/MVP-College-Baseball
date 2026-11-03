using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using MVPSetup;
static class Checks {
static void Check(bool b,string name){Console.WriteLine((b?"PASS ":"FAIL ")+name);if(!b)throw new Exception(name);}
static string Hash(string path){using var s=File.OpenRead(path);return Convert.ToHexStringLower(SHA256.HashData(s));}
static void Manifest(string dir){var rows=new JsonArray();foreach(var p in Directory.EnumerateFiles(dir,"*",SearchOption.AllDirectories).Where(p=>!p.EndsWith("port-manifest.json")).Order())rows.Add(new JsonObject{["path"]=Path.GetRelativePath(dir,p).Replace('\\','/'),["size"]=new FileInfo(p).Length,["sha256"]=Hash(p)});File.WriteAllText(Path.Combine(dir,"port-manifest.json"),new JsonObject{["version"]="0.2.2-beta",["files"]=rows}.ToJsonString());}
static bool Enabled(string dir)=>(bool)JsonNode.Parse(File.ReadAllText(Path.Combine(dir,"mods/mods.json")))!["order"]![0]!["enabled"]!;
static int Main(string[] args){try{
var output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
var port=Path.Combine(output,"package/Port Files");Directory.CreateDirectory(Path.Combine(port,"bundled-mods"));File.Copy(args[0],Path.Combine(port,BundledMods.CachePath),true);File.WriteAllText(Path.Combine(port,"bundled-mods/defaults.json"),"{\"format\":1,\"mvp27-ui\":true}");File.WriteAllText(Path.Combine(port,"MVP06.exe"),"fixture");Manifest(port);
var install=Path.Combine(output,"install");Directory.CreateDirectory(install);foreach(var f in Directory.EnumerateFiles(port,"*",SearchOption.AllDirectories)){var dst=Path.Combine(install,Path.GetRelativePath(port,f));Directory.CreateDirectory(Path.GetDirectoryName(dst)!);File.Copy(f,dst,true);}
BundledMods.Install(install);Check(Enabled(install),"new install enables UI by default");Check(Hash(Path.Combine(install,"mods/mvp27-ui.mvpmod"))==Hash(args[0]),"installed mod matches release bundle");
var list=new ModList(install);list.Reload();Check(list.Mods.Count==1&&list.Mods[0].Name=="MVP 27 UI"&&list.Mods[0].Enabled&&list.Mods[0].Error==null,"launcher reads and toggles ZIP64 UI mod");
list.Mods[0].Enabled=false;list.Save();File.Delete(Path.Combine(install,"mods/mvp27-ui.mvpmod"));BundledMods.Install(install);Check(!Enabled(install)&&File.Exists(Path.Combine(install,"mods/mvp27-ui.mvpmod")),"repair restores missing mod and preserves disabled toggle");
File.WriteAllText(Path.Combine(install,"mods/custom.txt"),"keep");var zip=Path.Combine(output,"release.zip");if(File.Exists(zip))File.Delete(zip);ZipFile.CreateFromDirectory(Path.Combine(output,"package"),zip);PortUpdater.Apply(install,zip);Check(!Enabled(install)&&File.ReadAllText(Path.Combine(install,"mods/custom.txt"))=="keep","update preserves toggle and unrelated mods");
// Fail inside bundled-mod publication and ensure the updater rolls port files back.
File.WriteAllText(Path.Combine(install,"MVP06.exe"),"old port");File.WriteAllText(Path.Combine(install,"mods/mods.json"),"invalid JSON");bool failed=false;try{PortUpdater.Apply(install,zip);}catch{failed=true;}Check(failed&&File.ReadAllText(Path.Combine(install,"MVP06.exe"))=="old port","bundled-mod failure rolls back update");
var folder=Path.Combine(output,"developer");Directory.CreateDirectory(Path.Combine(folder,"mods/dev-ui"));foreach(var f in Directory.EnumerateFiles(port,"*",SearchOption.AllDirectories)){var dst=Path.Combine(folder,Path.GetRelativePath(port,f));Directory.CreateDirectory(Path.GetDirectoryName(dst)!);File.Copy(f,dst,true);}File.WriteAllText(Path.Combine(folder,"mods/dev-ui/mod.json"),"{\"id\":\"mvp27-ui\"}");BundledMods.Install(folder);Check(!File.Exists(Path.Combine(folder,"mods/mvp27-ui.mvpmod")),"editable developer folder is preserved without duplicate ID");
return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
