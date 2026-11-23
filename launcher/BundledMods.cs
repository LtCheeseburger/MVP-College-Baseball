using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
namespace MVPSetup;

// Release caches are port files. Installed packages and the player's optional
// toggles are maintained separately, with one journaled config publication.
static class BundledMods
{
    public const string CachePath = "bundled-mods/mvp27-ui.mvpmod";
    static readonly (string Id,bool Required,bool AffectsSaves,bool Default)[] Packs =
    [("mvp27-league",true,true,true),("mvp27-logos",false,false,false),("mvp27-ui",false,false,true)];
    public static void Install(string dir,Action<string>? beforeWrite=null)
    {
        var available=Packs.Where(p=>File.Exists(Path.Combine(dir,"bundled-mods",p.Id+".mvpmod"))).ToArray();
        var installedManifest=PortUpdater.ReadManifest(dir);
        if(installedManifest?.Files.Any(f=>f.Path.Replace('\\','/')=="bundled-mods/mvp27-league.mvpmod")==true && !File.Exists(Path.Combine(dir,"bundled-mods/mvp27-league.mvpmod")))throw new InvalidDataException("Required league cache is missing. Update or re-extract the release before repairing.");
        if(available.Length==0)return;
        var manifest=installedManifest??throw new InvalidDataException("Missing port manifest for bundled mods.");
        var verified=new List<(string Id,bool Required,bool Default,string Cache,string Hash)>();
        foreach(var pack in available) {
            string relative="bundled-mods/"+pack.Id+".mvpmod",cache=Path.Combine(dir,relative);
            var entry=manifest.Files.SingleOrDefault(f=>f.Path.Replace('\\','/')==relative)??throw new InvalidDataException("Bundled mod is absent from the port manifest: "+pack.Id);
            using(var stream=File.OpenRead(cache))if(stream.Length!=entry.Size || Convert.ToHexStringLower(SHA256.HashData(stream))!=entry.Sha256)throw new InvalidDataException("Damaged bundled mod cache: "+pack.Id);
            using(var zip=ZipFile.OpenRead(cache))using(var reader=new StreamReader((zip.GetEntry("mod.json")??throw new InvalidDataException("Mod manifest missing.")).Open())) {
                var mod=JsonNode.Parse(reader.ReadToEnd());
                if((string?)mod?["id"]!=pack.Id || ((bool?)mod?["affectsSaves"]??false)!=pack.AffectsSaves || ((bool?)mod?["required"]??false)!=pack.Required || (string?)mod?["game"]?["xbeSha1"]!=Game.XbeSha1)throw new InvalidDataException("Bundled metadata mismatch: "+pack.Id);
            }
            verified.Add((pack.Id,pack.Required,pack.Default,cache,entry.Sha256));
        }
        var mods=Path.Combine(dir,"mods");Directory.CreateDirectory(mods);var configPath=Path.Combine(mods,"mods.json");
        var config=File.Exists(configPath)?JsonNode.Parse(File.ReadAllText(configPath)) as JsonObject??throw new InvalidDataException("Invalid mods.json; toggles were preserved."):new JsonObject{["format"]=1,["order"]=new JsonArray()};
        var order=config["order"] as JsonArray??throw new InvalidDataException("Invalid mod order.");bool changed=false;
        var defaults=Path.Combine(dir,"bundled-mods/defaults.json");var defaultState=File.Exists(defaults)?JsonNode.Parse(File.ReadAllText(defaults)):null;
        foreach(var pack in verified) {
            bool folder=Directory.EnumerateDirectories(mods).Any(path=>{try{return (string?)JsonNode.Parse(File.ReadAllText(Path.Combine(path,"mod.json")))?["id"]==pack.Id;}catch(Exception e)when(e is IOException or JsonException){return false;}});
            if(!folder) {
                string target=Path.Combine(mods,pack.Id+".mvpmod");bool same=false;
                if(File.Exists(target)){using var stream=File.OpenRead(target);same=Convert.ToHexStringLower(SHA256.HashData(stream))==pack.Hash;}
                if(!same){File.Copy(pack.Cache,target+".tmp",true);try{beforeWrite?.Invoke("mods/"+pack.Id+".mvpmod");File.Move(target+".tmp",target,true);}finally{if(File.Exists(target+".tmp"))File.Delete(target+".tmp");}}
            }
            var rows=order.Where(row=>(string?)row?["id"]==pack.Id).ToArray();
            if(rows.Length==0){order.Add(new JsonObject{["id"]=pack.Id,["enabled"]=pack.Required||((bool?)defaultState?[pack.Id]??pack.Default)});changed=true;}
            else if(pack.Required)foreach(var row in rows)if((bool?)row?["enabled"]!=true){row!["enabled"]=true;changed=true;}
        }
        if(changed){File.WriteAllText(configPath+".tmp",config.ToJsonString(new(){WriteIndented=true}));try{beforeWrite?.Invoke("mods/mods.json");File.Move(configPath+".tmp",configPath,true);}finally{if(File.Exists(configPath+".tmp"))File.Delete(configPath+".tmp");}}
    }
}
