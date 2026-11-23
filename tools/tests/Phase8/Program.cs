using System.Security.Cryptography;
using MVPWorkshop.Core.Formats;
if(args.Length is 5 or 6 && args[0]=="import") {
    var gamePath=args[1];var leaguePath=args[2];var importPath=args[3];var csvPath=args[4];
    var ledger=System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(Path.Combine(importPath,"players.json")))!.AsObject();
    var registry=System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(Path.Combine(importPath,"player-registry.json")))!.AsObject();
    var repeat=RosterCsvImporter.Import(csvPath,leaguePath,gamePath,registry,args.Length==6?args[5]:null);
    if(!System.Text.Json.Nodes.JsonNode.DeepEquals(ledger,repeat.Players)||!System.Text.Json.Nodes.JsonNode.DeepEquals(registry,repeat.Registry))throw new Exception("Import/identity registry is not stable.");
    var built=LeagueDatabase.Build(gamePath,leaguePath,true,Path.Combine(importPath,"players.json"));
    var real=ledger["teams"]!.AsObject().SelectMany(t=>t.Value!.AsArray()).Select(p=>p!).ToArray();
    int placeholderCount=ledger["placeholderTeams"]!.AsArray().Count;
    if(real.Length!=(args.Length==6?7515:7415)||built.Players!=real.Length+placeholderCount*25||built.RosterRows!=311||built.PlaceholderRosters!=(placeholderCount>0)||built.Audit["placeholderTeams"]!.AsArray().Count!=placeholderCount||(bool)built.Audit["productionReady"]!)throw new Exception("Roster coverage/labeling.");
    if(real.Select(p=>p["key"]!.ToString()).Distinct().Count()!=real.Length||real.Any(p=>p["sourceKind"]!.ToString()=="fictional"))throw new Exception("Duplicate/synthetic real roster entries.");
    var attrib=TaggedDat.Read(built.Files["data/database/database_big/attrib.dat"]);var left=TaggedDat.Read(built.Files["data/database/database_big/lhattrib.dat"]);var pitch=TaggedDat.Read(built.Files["data/database/database_big/pitcher.dat"]);
    foreach(var p in real){uint key=Convert.ToUInt32(p["key"]!.ToString(),16);if(attrib.Rows[key][0]!=p["firstName"]!.ToString()||left.Rows[key][2]!=p["sourceRatings"]!["contact"]!.ToString()||attrib.Rows[key][35]!=p["sourceRatings"]!["speed"]!.ToString())throw new Exception("Imported names/ratings differ from ledger.");bool pitcher=p["sourceFacts"]!["pos"]!.ToString() is "P" or "RHP" or "LHP";if(pitch.Rows.ContainsKey(key)!=pitcher)throw new Exception("Pitcher role/table mismatch.");}
    bool refused=false;try{LeagueDatabase.Build(gamePath,leaguePath,false,Path.Combine(importPath,"players.json"));}catch(InvalidDataException){refused=true;}if(refused!=(placeholderCount>0))throw new Exception("Placeholder opt-in validation.");
    var raw=built.Files["data/database/database_big/roster.bin"];foreach(var team in TeamRoster.Read(raw,true))if(team.PlayerCount is <9 or >25)throw new Exception("Native squad limit.");
    var original=File.ReadAllBytes(Path.Combine(gamePath,"data/database/database_big/roster.bin"));
    uint Word(byte[] bytes,int at)=>System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(at,4));
    int templateAt=4+487;
    var battingWords=Enumerable.Range(0,25).Select(i=>Word(original,templateAt+200+i*8)&0xffffu).Where(w=>(w&15)is >=2 and <=10).ToArray();
    foreach(var team in TeamRoster.Read(raw,true)){var actual=Enumerable.Range(0,team.PlayerCount).Select(i=>Word(raw,team.Offset+200+i*8)&0xffffu).ToList();foreach(var word in battingWords)if(!actual.Remove(word))throw new Exception("Native batting order lost/duplicated for "+team.Name);}
    foreach(var team in TeamRoster.Read(raw,true))for(int i=0;i<3;i++){int slot=raw[team.Offset+196+team.PlayerCount*8+i];if(slot>=team.PlayerCount||!pitch.Rows.ContainsKey(Word(raw,team.Offset+196+slot*8)))throw new Exception("Unresolved native rotation for "+team.Name);}
    var graduates=System.Text.Encoding.UTF8.GetString(built.Files["data/league/graduate-classes.csv"]).Split('\n',StringSplitOptions.RemoveEmptyEntries).Skip(1).Select(l=>l.Trim()).ToHashSet();
    foreach(var p in real){int year=(int)p["modernClass"]!;if(year is <0 or >4||attrib.Rows[Convert.ToUInt32(p["key"]!.ToString(),16)][9]!=Math.Min(year,3).ToString()||graduates.Contains(p["key"]!.ToString()+",4")!=(year==4))throw new Exception("Source graduate class overlay differs.");}
    if(repeat.Audit["ambiguousSharedStatsLinks"]!.AsArray().Count!=0||repeat.Audit["quarantinedSharedStatsRatings"]!.AsArray().Count!=24)throw new Exception("Shared-statistics quarantine differs.");
    foreach(var p in real.Where(p=>p["ratingSource"]!.ToString()=="native_role_template_ambiguous_stats"))if(p["sourceRatings"]!["stats_key"]!.ToString()!=""||p["suppliedRatings"]!["stats_key"]!.ToString()=="")throw new Exception("Ambiguous statistics applied or source evidence lost.");
    Console.WriteLine($"PASS {ledger["teams"]!.AsObject().Count} sourced squads / {real.Length} real players / {placeholderCount} placeholder squads / {built.Players} total; deterministic reimport and persistent keys; native names, batting ratings, speed, pitcher tables and squad counts verified");return;
}
if(args.Length==2 && args[0]=="logos") {
    int packs=0;
    foreach(var directory in Directory.EnumerateDirectories(args[1])) {
        var parts=Directory.EnumerateFiles(directory).ToDictionary(p=>Path.GetFileName(p),File.ReadAllBytes);
        var raw=XshFolder.Build(parts["xsh.meta"],n=>parts[n],out var failure)??throw new Exception(failure);
        int Read(int p)=>System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(raw.AsSpan(p,4));
        for(int image=0;image<Read(8);image++) {
            string tag=System.Text.Encoding.ASCII.GetString(raw,16+image*8,4);int pointer=Read(20+image*8);bool named=false;
            for(int block=0;block<32 && pointer>=0 && pointer+4<raw.Length;block++) {
                int header=Read(pointer);
                if((header&255)==0x70){int end=Array.IndexOf(raw,(byte)0,pointer+4);if(end<0)throw new Exception("Unterminated texture name");string name=System.Text.Encoding.ASCII.GetString(raw,pointer+4,end-pointer-4);if(name!=tag)throw new Exception($"{directory}: tag {tag}, attachment {name}");named=true;break;}
                int size=header>>8;if(size<4)break;pointer+=size;
            }
            if(!named)throw new Exception($"{directory}: native name chain missing for {tag}");
        }
        packs++;
    }
    Console.WriteLine($"PASS {packs} native name chains");return;
}
if(args.Length!=3)throw new ArgumentException("Phase8 <stock loose game> <league sources> <output>");
var(game,league,dest)=(args[0],args[1],args[2]);
string Hash(string p)=>Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(p)));
var source=Path.Combine(game,"data/database/database_big/roster.bin");var before=Hash(source);
bool rejected=false;try{LeagueDatabase.Build(game,league);}catch(InvalidDataException){rejected=true;}if(!rejected)throw new Exception("Missing real data accepted without explicit test flag.");
var result=LeagueDatabase.Build(game,league,true);
if(result.Teams!=309 || result.RosterRows!=311 || result.Players!=309*25 || !result.PlaceholderRosters)throw new Exception("League counts.");
var teams=TeamRoster.Read(result.Files["data/database/database_big/roster.bin"],true);
if(teams.Any(t=>t.RosterIndex==102) || teams.Max(t=>t.RosterIndex)!=310)throw new Exception("Retired identity gap not preserved.");
var definitions=LeagueDatabase.ReadTeams(league);var conf=TaggedDat.Read(result.Files["data/database/database_big/conf.dat"]);
var tokens=new Dictionary<string,int>();foreach(var group in definitions.GroupBy(t=>t.Conference))foreach(var(t,i)in group.OrderBy(t=>t.Index).Select((t,i)=>(t,i)))tokens.Add(conf.Rows[(uint)group.Key][1]+(i+1),t.Index);
var games=TaggedDat.CsvRecords(System.Text.Encoding.Latin1.GetString(result.Files["data/database/schedule_big/intrconf.dat"]));
if(games.Count!=8588)throw new Exception("Schedule game total.");
var played=definitions.ToDictionary(t=>t.Index,_=>0);var days=new HashSet<(int,string)>();
foreach(var gameRow in games)foreach(var key in new[]{"HOME","ROAD"}){int team=tokens[gameRow[key]];played[team]++;if(!days.Add((team,gameRow["DATE"])))throw new Exception("Double booking.");}
foreach(var team in definitions)if(played[team.Index]!=(team.Conference==22?40:56))throw new Exception("School game budget.");
var originalFolder=Path.Combine(game,"data/frontend/logos_big/l201.xsh");var originalFiles=Directory.EnumerateFiles(originalFolder).ToDictionary(p=>Path.GetFileName(p),File.ReadAllBytes);
var renamed=XshFolder.RetagSchool(originalFiles,400);var rebuilt=XshFolder.Build(renamed["xsh.meta"],n=>renamed[n],out var error)??throw new Exception(error);
var originalBytes=XshFolder.Build(originalFiles["xsh.meta"],n=>originalFiles[n],out error)??throw new Exception(error);var first=SshFile.Read(originalBytes);var last=SshFile.Read(rebuilt);
for(int i=0;i<first.Images.Count;i++)if(!last.Images[i].Tag.EndsWith("400") || !first.Decode(first.Images[i]).Bgra.SequenceEqual(last.Decode(last.Images[i]).Bgra))throw new Exception("Retag changed image data.");
var second=LeagueDatabase.Build(game,league,true);
if(result.Files.Any(p=>!p.Value.SequenceEqual(second.Files[p.Key])))throw new Exception("Non-deterministic build.");
if(Hash(source)!=before)throw new Exception("Source changed.");
Directory.CreateDirectory(dest);
foreach(var(p,data)in result.Files){var path=Path.Combine(dest,"files",p);Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.WriteAllBytes(path,data);}
File.WriteAllText(Path.Combine(dest,"audit.json"),result.Audit.ToJsonString(new(){WriteIndented=true}));
Console.WriteLine($"PASS 309 teams / 311 stable roster rows / {result.Players} unique owned players / {result.Pitchers} pitcher rows; 8,588 baseline games, exact 56/40 budgets and no double bookings; lossless native retag; strict missing-data refusal, deterministic output, untouched original, original-layout round trips");
