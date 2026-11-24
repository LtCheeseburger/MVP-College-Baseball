using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using MVPSetup;
static class Checks {
static void Check(bool b,string name){Console.WriteLine((b?"PASS ":"FAIL ")+name);if(!b)throw new Exception(name);}
static string Hash(string path){using var s=File.OpenRead(path);return Convert.ToHexStringLower(SHA256.HashData(s));}
static void Manifest(string dir){var rows=new JsonArray();foreach(var p in Directory.EnumerateFiles(dir,"*",SearchOption.AllDirectories).Where(p=>!p.EndsWith("port-manifest.json")).Order())rows.Add(new JsonObject{["path"]=Path.GetRelativePath(dir,p).Replace('\\','/'),["size"]=new FileInfo(p).Length,["sha256"]=Hash(p)});File.WriteAllText(Path.Combine(dir,"port-manifest.json"),new JsonObject{["version"]="0.2.6-beta",["files"]=rows}.ToJsonString());}
static JsonArray Order(string dir)=>JsonNode.Parse(File.ReadAllText(Path.Combine(dir,"mods/mods.json")))!["order"]!.AsArray();
static bool Enabled(string dir,string id)=>(bool?)Order(dir).Single(r=>(string?)r?["id"]==id)?["enabled"]==true;
static void CopyPort(string port,string dir){foreach(var f in Directory.EnumerateFiles(port,"*",SearchOption.AllDirectories)){var dst=Path.Combine(dir,Path.GetRelativePath(port,f));Directory.CreateDirectory(Path.GetDirectoryName(dst)!);File.Copy(f,dst,true);}}
static int Main(string[] args){try{
var output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
var port=Path.Combine(output,"package/Port Files");Directory.CreateDirectory(Path.Combine(port,"bundled-mods"));File.Copy(args[0],Path.Combine(port,BundledMods.CachePath),true);File.WriteAllText(Path.Combine(port,"bundled-mods/defaults.json"),"{\"format\":1,\"mvp27-overhaul\":true}");File.WriteAllText(Path.Combine(port,"MVP06.exe"),"fixture");Manifest(port);
var install=Path.Combine(output,"install");CopyPort(port,install);
BundledMods.Install(install);Check(Enabled(install,"mvp27-overhaul")&&Order(install).Count==1,"new install enables only Overhaul");Check(Hash(Path.Combine(install,"mods/mvp27-overhaul.mvpmod"))==Hash(args[0]),"installed mod matches release bundle");
var list=new ModList(install);list.Reload();Check(list.Mods.Count==1&&list.Mods[0].Name=="MVP 27 Overhaul"&&list.Mods[0].Enabled&&list.Mods[0].Required&&list.Mods[0].Error==null,"launcher validates required combined archive");
File.Delete(Path.Combine(install,"mods/mvp27-overhaul.mvpmod"));BundledMods.Install(install);Check(File.Exists(Path.Combine(install,"mods/mvp27-overhaul.mvpmod")),"repair restores missing Overhaul");
var order=Order(install);order[0]!["enabled"]=false;foreach(var id in new[]{"mvp27-league","mvp27-logos","mvp27-ui","mvp27-annual-schedules","mvp27-competition","mvp27-standings-awards","mvp27-modern-dynasty","mvp27-saves-launcher","custom-pack"})order.Add(new JsonObject{["id"]=id,["enabled"]=true});File.WriteAllText(Path.Combine(install,"mods/mods.json"),new JsonObject{["format"]=1,["order"]=order.DeepClone()}.ToJsonString());
var legacyManifest="{\"format\":1,\"layout\":1,\"id\":\"mvp27-league\",\"name\":\"Legacy League\",\"version\":\"1.0.0\",\"required\":true,\"game\":{\"xbeSha1\":\""+Game.XbeSha1+"\"}}";
var legacy=Path.Combine(install,"mods/legacy-league.mvpmod");using(var legacyZip=ZipFile.Open(legacy,ZipArchiveMode.Create))using(var writer=new StreamWriter(legacyZip.CreateEntry("mod.json").Open()))writer.Write(legacyManifest);
Directory.CreateDirectory(Path.Combine(install,"mods/legacy-ui"));File.WriteAllText(Path.Combine(install,"mods/legacy-ui/mod.json"),legacyManifest.Replace("mvp27-league","mvp27-ui"));
File.WriteAllText(Path.Combine(install,"mods/custom.txt"),"keep");var zip=Path.Combine(output,"release.zip");if(File.Exists(zip))File.Delete(zip);ZipFile.CreateFromDirectory(Path.Combine(output,"package"),zip);PortUpdater.Apply(install,zip);
Check(Enabled(install,"mvp27-overhaul")&&Order(install).Where(r=>(string?)r?["id"] is not "mvp27-overhaul" and not "custom-pack").All(r=>(bool?)r?["enabled"]==false),"upgrade enables Overhaul and disables all replaced component packs");Check(File.Exists(legacy+".disabled")&&!File.Exists(legacy)&&File.Exists(Path.Combine(install,"mods/legacy-ui/mod.json.disabled")),"upgrade preserves legacy archives and folders outside discovery");list.Reload();Check(list.Mods.Count==1&&list.Problems().Count==0,"migrated launcher has only Overhaul and no conflicts");Check(Enabled(install,"custom-pack")&&File.ReadAllText(Path.Combine(install,"mods/custom.txt"))=="keep","update preserves unrelated mods and preferences");
File.WriteAllText(Path.Combine(install,"MVP06.exe"),"old port");File.WriteAllText(Path.Combine(install,"mods/mods.json"),"invalid JSON");bool failed=false;try{PortUpdater.Apply(install,zip);}catch{failed=true;}Check(failed&&File.ReadAllText(Path.Combine(install,"MVP06.exe"))=="old port","bundled-mod failure rolls back update");
var folder=Path.Combine(output,"developer");CopyPort(port,folder);Directory.CreateDirectory(Path.Combine(folder,"mods/dev-overhaul"));File.WriteAllText(Path.Combine(folder,"mods/dev-overhaul/mod.json"),"{\"id\":\"mvp27-overhaul\"}");BundledMods.Install(folder);Check(!File.Exists(Path.Combine(folder,"mods/mvp27-overhaul.mvpmod")),"editable developer folder is preserved without duplicate ID");
File.Delete(Path.Combine(folder,BundledMods.CachePath));failed=false;try{BundledMods.Install(folder);}catch(InvalidDataException){failed=true;}Check(failed,"missing declared required cache refuses repair");
return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
