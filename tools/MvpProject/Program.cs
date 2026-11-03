using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

static class Program
{
    static readonly JsonSerializerOptions Pretty = new() { WriteIndented = true };
    static string Hash(string p) { using var s=File.OpenRead(p); return Convert.ToHexStringLower(SHA256.HashData(s)); }
    static string Safe(string root,string relative) {
        if(string.IsNullOrWhiteSpace(relative) || relative.Contains('\\') || relative.Split('/').Any(x=>x is "" or "." or "..") || relative.Contains(':'))throw new InvalidDataException("Unsafe relative path: "+relative);
        var p=Path.GetFullPath(Path.Combine(root,relative));
        if(!p.StartsWith(Path.GetFullPath(root)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException(relative);
        for(var item=p; item!=Path.GetFullPath(root); item=Path.GetDirectoryName(item)!)
            if((File.Exists(item)||Directory.Exists(item)) && (File.GetAttributes(item)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Linked input: "+relative);
        return p;
    }
    static JsonObject Read(string path)=>JsonNode.Parse(File.ReadAllText(path)) as JsonObject??throw new InvalidDataException(path);
    [STAThread] static int Main(string[] args) {
        try {
            if(args.Length!=1)throw new ArgumentException("Usage: MvpProject <folder.mvpproject>");
            var root=Path.TrimEndingDirectorySeparator(Path.GetFullPath(args[0]));var project=Read(Path.Combine(root,"project.json"));
            if((int?)project["format"]!=1)throw new InvalidDataException("Unsupported project format");
            var mod=project["mod"] as JsonObject??throw new InvalidDataException("Missing mod metadata");
            var id=(string?)mod["id"]??"";
            if(id.Length==0||id.Any(c=>!(char.IsAsciiLetterLower(c)||char.IsAsciiDigit(c)||c=='-')))throw new InvalidDataException("Invalid mod id");
            if((int?)mod["format"]!=1 || (int?)mod["layout"]!=1 || (string?)mod["game"]?["xbeSha1"] is not {Length:40})throw new InvalidDataException("Invalid mod metadata");
            var recipes=Read(Path.Combine(root,"recipes.json"));if((int?)recipes["format"]!=1)throw new InvalidDataException("Unsupported recipes format");
            var build=Path.Combine(root,"build");Directory.CreateDirectory(build);
            if((File.GetAttributes(root)&FileAttributes.ReparsePoint)!=0 || (File.GetAttributes(build)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Project/build directory must not be a link");
            var stage=Path.Combine(build,id+".stage-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);
            try {
                var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);var inputs=new JsonArray();
                foreach(var row in recipes["recipes"] as JsonArray??throw new InvalidDataException("No recipes")) {
                    var r=row as JsonObject??throw new InvalidDataException("Recipe is not an object");
                    var source=(string?)r["source"]??"";var target=(string?)r["target"]??"";
                    if(!(source.StartsWith("sources/")||source.StartsWith("overrides/")))throw new InvalidDataException("Input must be in sources/ or overrides/");
                    if(!(target.StartsWith("files/")||target is "icon.png" or "readme.md")||!seen.Add(target))throw new InvalidDataException("Invalid or duplicate target: "+target);
                    var input=Safe(root,source);var output=Safe(stage,target);Directory.CreateDirectory(Path.GetDirectoryName(output)!);
                    var op=(string?)r["op"];
                    if(op=="xsh-logo") {
                        var template=(string?)r["template"]??"";
                        if(!template.StartsWith("overrides/"))throw new InvalidDataException("Native template must be an override");
                        var templatePath=Safe(root,template);
                        if((string?)r["mainFormat"]!="DXT3" || (string?)r["wideFormat"]!="DXT3" || (string?)r["indexedFormat"]!="PAL8" || (int?)r["paletteColors"]!=256 || r["mipLevels"]?.ToString() is not ("full" or "1"))throw new InvalidDataException("Unsupported native-logo format or mip settings");
                        foreach(var member in Directory.EnumerateFileSystemEntries(templatePath,"*",SearchOption.AllDirectories))Safe(root,Path.GetRelativePath(root,member).Replace('\\','/'));
                        var templateHashes=new JsonObject();
                        foreach(var member in Directory.EnumerateFiles(templatePath).OrderBy(p=>p,StringComparer.Ordinal))templateHashes[Path.GetFileName(member)]=Hash(member);
                        var generated=NativeLogos.Build(input,templatePath,(int?)r["mainSize"]??128,(int?)r["wideWidth"]??64,(int?)r["wideHeight"]??32,r["mipLevels"]!.ToString()=="full");
                        Directory.CreateDirectory(output);
                        foreach(var file in generated.OrderBy(x=>x.Key,StringComparer.Ordinal)) {
                            var destination=target+"/"+file.Key;
                            if(!seen.Add(destination))throw new InvalidDataException("Duplicate native output: "+destination);
                            var path=Safe(stage,destination);File.WriteAllBytes(path,file.Value);
                            inputs.Add(new JsonObject { ["source"]=source,["template"]=template,["templateSha256"]=templateHashes.DeepClone(),["target"]=destination,["sourceSha256"]=Hash(input),["outputSha256"]=Hash(path),["bytes"]=file.Value.Length });
                        }
                        Console.WriteLine("Native logo: "+Path.GetFileName(target));
                        continue;
                    }
                    else if(op=="copy")File.Copy(input,output);
                    else if(op=="image") {
                        int width=(int?)r["width"]??0,height=(int?)r["height"]??0;
                        if(width<1||height<1||width>8192||height>8192||(string?)r["format"]!="png"||(int?)r["mipLevels"]!=1||(int?)r["padding"]!=0)throw new InvalidDataException("Unsupported image recipe");
                        using var stream=File.OpenRead(input);
                        var frame=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad).Frames[0];
                        var visual=new DrawingVisual();using(var dc=visual.RenderOpen()) {
                            RenderOptions.SetBitmapScalingMode(visual,BitmapScalingMode.HighQuality);
                            var rect=new Rect(0,0,width,height);
                            var fit=(string?)r["fit"]??"stretch";
                            if(fit=="contain") {
                                double scale=Math.Min((double)width/frame.PixelWidth,(double)height/frame.PixelHeight);
                                rect=new Rect((width-frame.PixelWidth*scale)/2,(height-frame.PixelHeight*scale)/2,frame.PixelWidth*scale,frame.PixelHeight*scale);
                            } else if(fit!="stretch")throw new InvalidDataException("Unsupported fit");
                            dc.DrawImage(frame,rect);
                        }
                        var bitmap=new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
                        var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using var file=File.Create(output);encoder.Save(file);
                    } else throw new InvalidDataException("Unsupported operation: "+op);
                    inputs.Add(new JsonObject { ["source"]=source,["target"]=target,["sourceSha256"]=Hash(input),["outputSha256"]=Hash(output),["bytes"]=new FileInfo(output).Length });
                }
                File.WriteAllText(Path.Combine(stage,"mod.json"),mod.ToJsonString(Pretty)+"\n",new UTF8Encoding(false));
                var folder=Path.Combine(build,id);var old=folder+".previous";
                foreach(var target in new[]{folder,old})
                    if(Directory.Exists(target) && (File.GetAttributes(target)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Linked build output");
                // All replacements are beneath the caller-selected project build/ directory.
                if(Directory.Exists(old))Directory.Delete(old,true);
                if(Directory.Exists(folder))Directory.Move(folder,old);
                Directory.Move(stage,folder);
                if(Directory.Exists(old))Directory.Delete(old,true);
                var archive=Path.Combine(build,id+".mvpmod");Zip64.Write(folder,archive+".tmp");File.Move(archive+".tmp",archive,true);
                File.WriteAllText(archive+".sha256",Hash(archive)+" *"+Path.GetFileName(archive)+"\n");
                var report=new JsonObject { ["format"]=1,["id"]=id,["version"]=mod["version"]!.DeepClone(),["archiveSha256"]=Hash(archive),["archiveBytes"]=new FileInfo(archive).Length,["inputs"]=inputs };
                File.WriteAllText(Path.Combine(build,"build-manifest.json"),report.ToJsonString(Pretty));
                Console.WriteLine(archive);Console.WriteLine($"{inputs.Count} generated files; {new FileInfo(archive).Length:N0} bytes; stored ZIP64");
            } finally {if(Directory.Exists(stage))Directory.Delete(stage,true);}
            return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
}
static class Zip64 {
    record Entry(string Name,string File,long Offset,long Size,uint Crc);
    static uint Crc(string path) {
        uint value=0xffffffff;using var f=File.OpenRead(path);var buffer=new byte[65536];int n;
        while((n=f.Read(buffer))>0)for(int i=0;i<n;i++){value^=buffer[i];for(int bit=0;bit<8;bit++)value=(value>>1)^((value&1)!=0?0xedb88320u:0);}
        return ~value;
    }
    public static void Write(string root,string path) {
        using var writer=new BinaryWriter(File.Create(path),Encoding.UTF8);var entries=new List<Entry>();
        foreach(var file in Directory.EnumerateFiles(root,"*",SearchOption.AllDirectories).OrderBy(p=>Path.GetRelativePath(root,p),StringComparer.Ordinal)) {
            string name=Path.GetRelativePath(root,file).Replace('\\','/');var bytes=Encoding.UTF8.GetBytes(name);long size=new FileInfo(file).Length,offset=writer.BaseStream.Position;uint crc=Crc(file);
            writer.Write(0x04034b50u);writer.Write((ushort)45);writer.Write((ushort)0x800);writer.Write((ushort)0);writer.Write((ushort)0);writer.Write((ushort)33);writer.Write(crc);writer.Write(uint.MaxValue);writer.Write(uint.MaxValue);writer.Write((ushort)bytes.Length);writer.Write((ushort)20);writer.Write(bytes);writer.Write((ushort)1);writer.Write((ushort)16);writer.Write((ulong)size);writer.Write((ulong)size);
            using(var input=File.OpenRead(file))input.CopyTo(writer.BaseStream);
            entries.Add(new(name,file,offset,size,crc));
        }
        long start=writer.BaseStream.Position;
        foreach(var e in entries) {
            var name=Encoding.UTF8.GetBytes(e.Name);writer.Write(0x02014b50u);writer.Write((ushort)45);writer.Write((ushort)45);writer.Write((ushort)0x800);writer.Write((ushort)0);writer.Write((ushort)0);writer.Write((ushort)33);writer.Write(e.Crc);writer.Write(uint.MaxValue);writer.Write(uint.MaxValue);writer.Write((ushort)name.Length);writer.Write((ushort)28);writer.Write((ushort)0);writer.Write((ushort)0);writer.Write((ushort)0);writer.Write(0u);writer.Write(uint.MaxValue);writer.Write(name);writer.Write((ushort)1);writer.Write((ushort)24);writer.Write((ulong)e.Size);writer.Write((ulong)e.Size);writer.Write((ulong)e.Offset);
        }
        long end=writer.BaseStream.Position;
        writer.Write(0x06064b50u);writer.Write(44ul);writer.Write((ushort)45);writer.Write((ushort)45);writer.Write(0u);writer.Write(0u);writer.Write((ulong)entries.Count);writer.Write((ulong)entries.Count);writer.Write((ulong)(end-start));writer.Write((ulong)start);
        writer.Write(0x07064b50u);writer.Write(0u);writer.Write((ulong)end);writer.Write(1u);
        writer.Write(0x06054b50u);writer.Write((ushort)0);writer.Write((ushort)0);writer.Write(ushort.MaxValue);writer.Write(ushort.MaxValue);writer.Write(uint.MaxValue);writer.Write(uint.MaxValue);writer.Write((ushort)0);
    }
}
