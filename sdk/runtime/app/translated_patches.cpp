#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include "app/translated_patches.h"
#include "core/patch_json.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#pragma comment(lib, "bcrypt.lib")

namespace translated_patches {
namespace {
namespace fs = std::filesystem;
using patch_json::Json;
std::string Read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read patch input: " + p.string());
    std::string text((std::istreambuf_iterator<char>(in)), {});
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}
std::vector<fs::path> Files(const fs::path& dir) {
    std::vector<fs::path> paths;
    if (!fs::exists(dir)) return paths;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file() && entry.path().extension() == ".json") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    return paths;
}
const Json& Array(const Json& value, const char* key) {
    const auto* array = value.Get(key);
    if (!array || array->type != Json::Array) throw std::runtime_error(std::string("Patch array missing: ") + key);
    return *array;
}
std::string Required(const Json& value, const char* key) {
    auto s = value.Str(key);
    if (s.empty()) throw std::runtime_error(std::string("Patch string missing: ") + key);
    return s;
}
struct Function { size_t file, start, end; std::string original, edited; };
struct Source { fs::path path; std::string text; };
std::pair<size_t,size_t> Span(const std::string& body, const std::string& at) {
    auto p = body.find("// " + at + " ");
    if (p == std::string::npos) throw std::runtime_error("Patch instruction missing: " + at);
    size_t a = body.find('\n', p) + 1, b = a;
    while (b < body.size()) {
        auto next = body.find('\n', b);
        if (next == std::string::npos) next = body.size();
        auto begin = body.find_first_not_of(" \t", b);
        if (begin < next && (body.compare(begin,3,"// ") == 0 || body.compare(begin,4,"loc_") == 0 || body[begin] == '}')) break;
        b = next < body.size() ? next + 1 : next;
    }
    return {a,b};
}
size_t Replace(std::string& text, const std::string& from, const std::string& to, bool token) {
    if (from.empty()) throw std::runtime_error("Empty patch literal");
    size_t count = 0, pos = 0;
    while ((pos = text.find(from,pos)) != std::string::npos) {
        auto end = pos + from.size();
        if (token && end < text.size() && (std::isalnum(uint8_t(text[end])) || text[end]=='_')) {pos=end;continue;}
        text.replace(pos,from.size(),to);pos+=to.size();count++;
    }
    return count;
}
}

std::string Sha256(const std::string& text) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 unavailable");
    uint8_t digest[32];
    auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),ULONG(text.size()),digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(result<0) throw std::runtime_error("SHA256 failed");
    std::string hex;
    for(auto byte:digest){hex+="0123456789abcdef"[byte>>4];hex+="0123456789abcdef"[byte&15];}
    return hex;
}

std::string Revision(const fs::path& manifests) {
    std::string identity;
    for(const auto& path:Files(manifests)) identity+=path.filename().string()+"\n"+Sha256(Read(path))+"\n";
    return Sha256(identity);
}

void Apply(const fs::path& generated, const fs::path& manifests) {
    auto files=Files(manifests);
    if(files.empty()) return;
    std::vector<Json> documents;
    size_t editCount=0;
    std::set<std::string> includes;
    std::string condition;
    for(const auto& path:files) {
        Json document;
        if(!patch_json::ParseJson(Read(path),document) || document.Num("format",0)!=1) throw std::runtime_error("Invalid patch manifest: "+path.string());
        const auto& edits=Array(document,"edits");editCount+=edits.items.size();
        if(!edits.items.empty()) {
            auto value=Required(document,"condition");
            if(!condition.empty() && condition!=value) throw std::runtime_error("Conflicting patch conditions");
            condition=value;
            for(auto& include:document.Strings("includes")) {
                if(include.find_first_of("\"\n\r")!=std::string::npos) throw std::runtime_error("Invalid patch include");
                includes.insert(include);
            }
        }
        documents.push_back(std::move(document));
    }
    if(!editCount) return;
    const auto marker="// MVP06_PATCH_REV "+Revision(manifests)+"\n";
    std::vector<Source> sources;
    for(const auto& entry:fs::directory_iterator(generated)) {
        auto name=entry.path().filename().string();
        if(name.starts_with("x86_recomp.") && name.ends_with(".cpp")) sources.push_back({entry.path(),Read(entry.path())});
    }
    std::sort(sources.begin(),sources.end(),[](auto& a,auto& b){return a.path<b.path;});
    if(sources.empty()) throw std::runtime_error("No translated game partitions");
    bool already=false, plain=false;
    for(auto& source:sources) {
        if(source.text.starts_with(marker)) already=true;
        else if(source.text.starts_with("// MVP06_PATCH_REV ")) throw std::runtime_error("Patch revision changed: retranslate before patching");
        else plain=true;
    }
    if(already && !plain) return;
    if(already) throw std::runtime_error("Interrupted patch publication: retranslate before patching");
    std::map<std::string,Function> functions;
    constexpr std::string_view head="X86_FUNC(__imp__sub_";
    for(size_t f=0;f<sources.size();f++) {
        auto& text=sources[f].text;size_t at=0;
        while((at=text.find(head,at))!=std::string::npos) {
            auto name=text.substr(at+head.size(),8);
            if(text.compare(at+head.size()+8,4,") {\n")!=0){at+=head.size();continue;}
            auto end=text.find("\n}\n",at);
            if(end==std::string::npos) throw std::runtime_error("Unterminated translated function");
            end+=3;
            auto body=text.substr(at,end-at);
            if(!functions.emplace(name,Function{f,at,end,body,body}).second) throw std::runtime_error("Duplicate translated function "+name);
            at=end;
        }
    }
    // Validation is against untouched translations, before any replacements.
    for(const auto& document:documents) for(const auto& edit:Array(document,"edits").items) {
        auto at=Required(edit,"at");
        std::set<std::string> expected;
        for(const auto& owner:Array(edit,"functions").items) {
            auto name=Required(owner,"function");
            if(!expected.insert(name).second || !functions.count(name)) throw std::runtime_error("Missing/duplicate patch function "+name);
            auto& body=functions.at(name).original;
            auto [a,b]=Span(body,at);
            auto region=edit.Flag("all") ? body : body.substr(a,b-a);
            if(Sha256(region)!=Required(owner,"translation_sha256")) throw std::runtime_error("Translation hash mismatch at "+at+" in "+name);
            if(owner.Num("occurrences",0)<1) throw std::runtime_error("Invalid occurrence count at "+at);
        }
        if(expected.empty()) throw std::runtime_error("Patch has no owners at "+at);
        if(!edit.Flag("restricted")) {
            std::set<std::string> actual;
            for(auto& [name,fn]:functions) if(fn.original.find("// "+at+" ")!=std::string::npos) actual.insert(name);
            if(expected!=actual) throw std::runtime_error("Instruction owner mismatch at "+at);
        }
    }
    std::set<std::string> changed;
    for(const auto& document:documents) for(const auto& edit:Array(document,"edits").items) {
        auto at=Required(edit,"at");
        for(const auto& owner:Array(edit,"functions").items) {
            auto name=Required(owner,"function");auto& body=functions.at(name).edited;
            auto [a,b]=Span(body,at);size_t count=0;
            if(auto* replacement=edit.Get("replacement")) {
                if(replacement->type!=Json::String)throw std::runtime_error("Invalid instruction replacement");
                auto authored="\t"+replacement->s+"\n";
                if(auto* guard=edit.Get("guard")) {
                    if(guard->type!=Json::String||guard->s.empty())throw std::runtime_error("Invalid instruction guard");
                    authored="\tif ("+guard->s+") {\n"+authored+"\t} else {\n"+body.substr(a,b-a)+"\t}\n";
                }
                body.replace(a,b-a,authored);count=1;
            } else if(auto* before=edit.Get("before")) {
                if(before->type!=Json::String) throw std::runtime_error("Invalid insertion");
                body.insert(a,"\t"+before->s+"\n");count=1;
            } else if(auto* after=edit.Get("after")) {
                if(after->type!=Json::String) throw std::runtime_error("Invalid insertion");
                body.insert(b,"\t"+after->s+"\n");count=1;
            } else if(edit.Flag("all")) count=Replace(body,Required(edit,"old"),Required(edit,"new"),true);
            else {
                auto from=Required(edit,"old"),to=Required(edit,"new");
                size_t line=a;
                while(line<b) {
                    auto end=body.find('\n',line);if(end==std::string::npos)end=body.size();
                    auto part=body.substr(line,end-line);
                    if(part.find(from)!=std::string::npos) {count=Replace(part,from,to,false);body.replace(line,end-line,part);break;}
                    line=end+1;
                }
            }
            if(count!=size_t(owner.Num("occurrences",0))) throw std::runtime_error("Patch occurrence mismatch at "+at+" in "+name);
            changed.insert(name);
        }
    }
    // Clone bodies and dispatch remain in game partitions, never in the host exe.
    std::map<size_t,std::vector<std::pair<size_t,std::string>>> replacements;
    for(const auto& name:changed) {
        auto& fn=functions.at(name);auto clone=fn.edited;
        const auto original="__imp__sub_"+name, expanded="sub_"+name+"_exp";
        clone.replace(clone.find(original),original.size(),expanded);
        auto dispatch=fn.original;auto brace=dispatch.find("{\n");
        dispatch.insert(brace+2,"\tif ("+condition+") return "+expanded+"(ctx, base);\n");
        replacements[fn.file].push_back({fn.start,clone+"\n"+dispatch});
    }
    for(auto& [f,edits]:replacements) {
        std::sort(edits.rbegin(),edits.rend());
        for(auto& [at,replacement]:edits) {
            auto end=sources[f].text.find("\n}\n",at)+3;
            sources[f].text.replace(at,end-at,replacement);
        }
    }
    // Mark every partition: mixed publication is rejected and retried from translation.
    for(auto& source:sources) {
        auto text=marker;
        for(const auto& include:includes) text+="#include \""+include+"\"\n";
        text+=source.text;
        auto temporary=source.path;temporary+=".patch.tmp";
        {std::ofstream out(temporary,std::ios::binary);out.write(text.data(),text.size());if(!out)throw std::runtime_error("Cannot write patched translation");}
        std::error_code ec;fs::rename(temporary,source.path,ec);
        if(ec) throw std::runtime_error("Cannot publish patched translation: "+ec.message());
    }
}
}
