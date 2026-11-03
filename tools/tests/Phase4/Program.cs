using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows;
using MVPWorkshop.Core.Formats;
using MVPWorkshop.Core.IO;
static class CheckNative {
static void Need(bool b,string s){if(!b)throw new Exception(s);}
static byte[] Rebuild(string dir)=>XshFolder.Build(File.ReadAllBytes(Path.Combine(dir,"xsh.meta")),name=>File.ReadAllBytes(Path.Combine(dir,name)),out var error)??throw new Exception(error);
[STAThread] static int Main(string[] args){try{
var project=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);var game=Path.GetFullPath(args[2]);
var inventory=JsonNode.Parse(File.ReadAllText(Path.Combine(project,"source-inventory.json")))!;
var roster=TeamRoster.Read(File.ReadAllBytes(Path.Combine(game,"data/database/database_big/roster.bin")),xbox:true);
var archive=File.ReadAllBytes(Path.Combine(output,"logos.big"));var big=BigArchive.Read(new MemoryByteSource(archive));int count=0;long total=0;var report=new JsonArray();
var visual=new DrawingVisual();using var draw=visual.RenderOpen();draw.DrawRectangle(new SolidColorBrush(Color.FromRgb(55,58,63)),null,new Rect(0,0,960,4*220));int sample=0;
foreach(var row in inventory["teams"]!.AsArray()){
 string id=(string)row!["id"]!,name="l"+id+".xsh";
 Need(roster.Single(t=>t.Name.Equals((string)row["team"]!,StringComparison.OrdinalIgnoreCase)).LogoId==int.Parse(id),"Stock roster logo assignment mismatch: "+id);
 var folder=Path.Combine(project,"build/mvp27-logos/files/data/frontend/logos_big",name);var cpp=File.ReadAllBytes(Path.Combine(output,name));
 Need(cpp.SequenceEqual(Rebuild(folder)),"C++/Workshop XSH mismatch: "+id);
 var entry=big.Entries.Single(x=>x.Name.Equals(name,StringComparison.OrdinalIgnoreCase));var packed=archive.AsSpan((int)entry.Offset,(int)entry.Size).ToArray();var unpacked=RefPack.IsCompressed(packed)?RefPack.Decompress(packed):packed;
 Need(cpp.SequenceEqual(unpacked),"Rebuilt BIG entry mismatch: "+id);
 var native=SshFile.Read(cpp);var i=native.Images.Single(x=>x.Tag=="i"+id);var j=native.Images.Single(x=>x.Tag=="j"+id);var a=native.Images.Single(x=>x.Tag=="a"+id);
 Need(i.Header.Width==256&&i.Header.Height==256&&i.TypeMasked==0x61&&i.Header.Mipmaps==8,"Main DDS metadata: "+id);
 Need(j.Header.Width==128&&j.Header.Height==64&&j.TypeMasked==0x61&&j.Header.Mipmaps==7,"Wide DDS metadata: "+id);
 var originalDir=Path.Combine(project,(string)row["template"]!);var original=SshFile.Read(Rebuild(originalDir));var oldA=original.Images.Single(x=>x.Tag==a.Tag);
 if(id is "001" or "048" or "112"){
  var px=original.Decode(original.Images.Single(x=>x.Tag==i.Tag));var bmp=BitmapSource.Create(px.Width,px.Height,96,96,PixelFormats.Bgra32,null,px.Bgra,px.Width*4);
  var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bmp));using var file=File.Create(Path.Combine(output,"original-"+id+".png"));png.Save(file);
 }
 Need(a.TypeMasked==0x7B&&a.Header.Width==oldA.Header.Width&&a.Header.Height==oldA.Header.Height&&a.Header.FlagSwizzled==oldA.Header.FlagSwizzled,"PAL8 storage changed: "+id);
 foreach(var f in Directory.EnumerateFiles(originalDir).Where(p=>Path.GetFileName(p).StartsWith("00_")||Path.GetFileName(p).StartsWith("03_")||Path.GetFileName(p).StartsWith("04_")))Need(File.ReadAllBytes(f).SequenceEqual(File.ReadAllBytes(Path.Combine(folder,Path.GetFileName(f)))),"Unrelated slot changed: "+id);
 foreach(var image in new[]{i,j,a}){var px=native.Decode(image).Bgra;Need(px.Where((_,n)=>n%4==3).Any(x=>x>0),"Blank replacement: "+id);}
 if(new[]{"001","048","103","112"}.Contains(id)&&sample<4){int col=0;foreach(var image in new[]{i,j,a}){var decoded=native.Decode(image);var bitmap=BitmapSource.Create(decoded.Width,decoded.Height,96,96,PixelFormats.Bgra32,null,decoded.Bgra,decoded.Width*4);double scale=Math.Min(300.0/decoded.Width,200.0/decoded.Height);draw.DrawImage(bitmap,new Rect(col*320+(320-decoded.Width*scale)/2,sample*220+(220-decoded.Height*scale)/2,decoded.Width*scale,decoded.Height*scale));col++;}sample++;}
 report.Add(new JsonObject{["id"]=id,["nativeBytes"]=cpp.Length,["sha256"]=Convert.ToHexStringLower(SHA256.HashData(cpp))});total+=cpp.Length;count++;
}
var uni=big.Entries.Single(x=>x.Name.Equals("l103.xsh",StringComparison.OrdinalIgnoreCase));var uniBytes=archive.AsSpan((int)uni.Offset,(int)uni.Size).ToArray();if(RefPack.IsCompressed(uniBytes))uniBytes=RefPack.Decompress(uniBytes);Need(uniBytes.SequenceEqual(Rebuild(Path.Combine(game,"data/frontend/logos_big/l103.xsh"))),"Northern Iowa changed");
draw.Close();var target=new RenderTargetBitmap(960,880,96,96,PixelFormats.Pbgra32);target.Render(visual);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(target));using(var f=File.Create(Path.Combine(output,"native-review.png")))encoder.Save(f);
File.WriteAllText(Path.Combine(output,"validation.json"),new JsonObject{["teams"]=count,["nativeXshBytes"]=total,["archiveBytes"]=archive.Length,["unchangedNorthernIowa"]=true,["files"]=report}.ToJsonString(new(){WriteIndented=true}));Console.WriteLine($"PASS {count} C++/Workshop native XSH and BIG comparisons; mip/dimension/palette checks; unrelated slots and Northern Iowa unchanged");return 0;
}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
