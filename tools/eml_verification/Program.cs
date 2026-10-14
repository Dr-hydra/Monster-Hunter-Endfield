using EModLoader.Core;
using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Nodes;

static void Require(bool value,string message){if(!value)throw new InvalidOperationException(message);}
static string? FindProjectRoot(string start){
 for(var directory=new DirectoryInfo(start);directory is not null;directory=directory.Parent){
  if(File.Exists(Path.Combine(directory.FullName,"module.json"))&&
     File.Exists(Path.Combine(directory.FullName,"CMakeLists.txt"))&&
     File.Exists(Path.Combine(directory.FullName,"tools","eml_verification","EmlVerification.csproj")))return directory.FullName;
 }
 return null;
}
if(args.Length is not (3 or 4))throw new ArgumentException("EmlVerification <final-package.zip> <offline-host.exe> <new-run-directory> [legacy-package.zip]");
var archive=Path.GetFullPath(args[0]);var executable=Path.GetFullPath(args[1]);var run=Path.GetFullPath(args[2]);
var projectRoot=FindProjectRoot(Path.GetDirectoryName(archive)!)??FindProjectRoot(AppContext.BaseDirectory)
 ??throw new InvalidOperationException("Cannot locate the project checkout; keep the package or this tool inside the project tree");
var allowed=Path.Combine(projectRoot,"local","verification")+Path.DirectorySeparatorChar;
Require(run.StartsWith(allowed,StringComparison.OrdinalIgnoreCase),"Verification output must stay in the project local/verification directory");
Require(!Directory.Exists(run)&&!File.Exists(run),"Use a new verification run directory; do not replace an existing module library");
for(var directory=new DirectoryInfo(Path.GetDirectoryName(run)!);directory is not null&&!directory.FullName.Equals(projectRoot,StringComparison.OrdinalIgnoreCase);directory=directory.Parent){
 Require(!directory.Exists||(directory.Attributes&FileAttributes.ReparsePoint)==0,"Verification output must not traverse a symbolic link or junction");
}
Directory.CreateDirectory(run);var log=new List<string>();var result=new JsonObject{["archive"]=archive,["offline_host"]=executable,["run_directory"]=run,["runtime_validation"]=false,["gameplay_validation"]=false};
try{
 var store=new ModuleStore(Path.Combine(run,"isolated-library"));await store.EnsureAsync();
 if(args.Length==4){
  var legacy=await store.ImportAsync(Path.GetFullPath(args[3]));Require(legacy.Supported,"Legacy EML package import failed");
  var legacyConfig=legacy.State["configuration"]!.DeepClone().AsObject();legacyConfig["sound_volume"]=.39;legacyConfig["spirit_key"]=17;legacyConfig["verification_note"]="preserve user configuration";
  await store.SaveConfigurationAsync(legacy.Id,legacyConfig);await store.SetEnabledAsync(legacy.Id,true);
  result["legacy_upgrade"]=new JsonObject{["from_version"]=legacy.Version,["previous_generation"]=legacy.Generation,["configuration"]=legacyConfig.DeepClone()};
 }
 var installed=await store.ImportAsync(archive);Require(installed.Supported&&installed.Error.Length==0,"Actual EML import did not produce a supported module");
 Require(installed.Id=="endfield.longsword.showcase","Unexpected module ID");using(var zip=ZipFile.OpenRead(archive)){
 var entry=zip.GetEntry("module.json")??throw new InvalidOperationException("Missing module.json");
 using var reader=new StreamReader(entry.Open());var manifest=JsonNode.Parse(await reader.ReadToEndAsync())!;
 Require(installed.Version==manifest["version"]!.GetValue<string>(),"Installed version differs from final package");
 }
 Require(installed.Directory.StartsWith(store.Root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"Import escaped isolated module store");
 Require(store.Modules().Count==1,"Isolated module store contains unexpected modules");
 if(result["legacy_upgrade"] is JsonObject upgrade){
  Require(installed.Generation!=upgrade["previous_generation"]!.GetValue<string>(),"Upgrade did not publish a new package generation");
  Require(installed.Enabled&&JsonNode.DeepEquals(installed.State["configuration"],upgrade["configuration"]),"Actual upgrade lost enabled state or user configuration");
  upgrade["to_version"]=installed.Version;upgrade["passed"]=true;
 }
 await store.SetEnabledAsync(installed.Id,true);
 result["import"]=new JsonObject{["id"]=installed.Id,["version"]=installed.Version,["supported"]=installed.Supported,["directory"]=installed.Directory};
 await using var host=new HostConnection(store);host.Log+=message=>{lock(log)log.Add(message);};
 await host.StartOfflineAsync(executable);Require(host.OwnsOfflineHost,"Harness did not own the newly launched offline host");
 var hostStatus=await host.RequestAsync("status",installed.Id);var module=hostStatus["module"]?.AsObject()??throw new InvalidOperationException("Actual host did not report imported module");
 Require(hostStatus["mode"]?.GetValue<string>()=="offline","Actual host is not in offline mode");
 Require(module["status"]?.GetValue<string>()=="ready","Actual native DLL failed to initialize: "+module["error"]?.GetValue<string>());
 result["host_module"]=module.DeepClone();
 async Task<JsonObject> Send(string action,int expectedResult,JsonObject? extra=null){
  var request="verify-"+Guid.NewGuid().ToString("N");var payload=extra?.DeepClone().AsObject()??new JsonObject();payload["action"]=action;payload["request_id"]=request;
  var accepted=await host.RequestAsync("send",installed.Id,payload,request);Require(accepted["accepted"]?.GetValue<bool>()==true,"Host did not accept "+action);
  var until=DateTime.UtcNow+TimeSpan.FromSeconds(10);
  while(DateTime.UtcNow<until){
   var poll=await host.RequestAsync("poll",installed.Id);
   foreach(var item in poll["messages"]?.AsArray()??new JsonArray()){
    if(item?["kind"]?.GetValue<string>()=="reply"&&item["request_id"]?.GetValue<string>()==request){
     Require(item["result"]?.GetValue<int>()==expectedResult,"Unexpected native result for "+action+": "+item["result"]?.ToJsonString());
     return item.DeepClone().AsObject();}
   }
   await Task.Delay(40);
  }
  throw new TimeoutException("No native reply for "+action);
 }
 var initial=await Send("status",0);var initialBody=initial["body"]?.AsObject()??throw new InvalidOperationException("Native status has no body");
 Require(initialBody["runtime_ready"]?.GetValue<bool>()==false,"Offline host unexpectedly claimed a live Unity runtime");
 Require(initialBody["active"]?.GetValue<bool>()==false,"Offline native module unexpectedly had an active character");
 result["initial_native_status"]=initialBody.DeepClone();
 var configuration=store.Find(installed.Id).State["configuration"]!.DeepClone().AsObject();configuration["sound_volume"]=.42;configuration["laevatain_blade"]=false;
 var saved=await WebBridge.DispatchAsync(store,host,installed.Id,"saveConfig",configuration,"verify-config");Require(saved?["saved"]?.GetValue<bool>()==true,"Actual EML WebBridge failed to save config");
 Require(store.Find(installed.Id).State["configuration"]?["sound_volume"]?.GetValue<double>()==.42,"Persisted configuration differs");
 var configured=await Send("status",0);Require(configured["body"]?["laevatain_blade"]?.GetValue<bool>()==false,"Actual DLL did not receive updated configuration");
 result["configuration"]=new JsonObject{["persisted_sound_volume"]=.42,["native_laevatain_blade"]=false,["webbridge_saved"]=true};
 var verifiedModes=new JsonArray();
 foreach(var modeId in new[]{"longsword","sword_shield","greatsword","lance","light_bowgun","bow"}){
  var switched=await Send("set_mode",0,new JsonObject{["mode"]=modeId});
  Require(switched["body"]?["mode"]?.GetValue<string>()==modeId,"DLL failed actual mode load "+modeId);
  Require(switched["body"]?["sound_ready"]?.GetValue<bool>()==true,"Mode PCM failed "+modeId);
  verifiedModes.Add(modeId);
 }
 result["verified_modes"]=verifiedModes;
 await Send("set_mode",3,new JsonObject{["mode"]="nonexistent"});
 var preserved=await Send("status",0);Require(preserved["body"]?["mode"]?.GetValue<string>()=="bow","Rejected mode corrupted previous state");
 await Send("pause",2);await Send("resume",2);await Send("preview",2,new JsonObject{["slot"]=51000});
 configuration["schema_version"]=2;configuration["mode"]="lance";configuration["hud_layout"]="compact";configuration["theme"]="high_contrast";
 saved=await WebBridge.DispatchAsync(store,host,installed.Id,"saveConfig",configuration,"verify-final-config");
 Require(saved?["saved"]?.GetValue<bool>()==true,"Multimode config persistence failed");
 var finalState=await Send("status",0);Require(finalState["body"]?["mode"]?.GetValue<string>()=="lance","Config not applied to mode");
 Require(finalState["body"]?["capabilities"]?["modes"]?.AsArray().Count==6,"Final capability registry incomplete");
 result["configuration_roundtrip"] = new JsonObject{["mode"]="lance",["layout"]="compact",["theme"]="high_contrast",["passed"]=true};
 result["start_demo_without_unity"]=await Send("demo",2);result["toggle_without_unity"]=await Send("toggle",2);
 var stopped=await Send("stop",0);Require(stopped["body"]?["active"]?.GetValue<bool>()==false,"Stop unexpectedly activated a character");result["stop_native"]=stopped;
 await host.StopOfflineAsync();Require(!host.OwnsOfflineHost,"Offline process did not stop");
 bool listenerStopped=false;try{await host.RequestAsync("status");}catch(HttpRequestException){listenerStopped=true;}Require(listenerStopped,"Owned offline HTTP listener remained active after shutdown");
 result["offline_host_stopped"]=true;
 await store.SetEnabledAsync(installed.Id,false);Require(!store.Find(installed.Id).Enabled,"Actual module disable was not persisted");
 await store.SetEnabledAsync(installed.Id,true);Require(store.Find(installed.Id).Enabled,"Actual module enable was not persisted");
 var retainedConfiguration=store.Find(installed.Id).State["configuration"]!.DeepClone();
 var replaced=await store.ImportAsync(archive);
 Require(replaced.Supported&&replaced.Enabled&&replaced.Generation!=installed.Generation&&store.Modules().Count==1,"Actual package reinstall duplicated or disabled the module");
 Require(JsonNode.DeepEquals(replaced.State["configuration"],retainedConfiguration),"Actual package reinstall lost saved configuration");
 await host.StartOfflineAsync(executable);var reinstalled=await host.RequestAsync("status",installed.Id);
 Require(reinstalled["module"]?["status"]?.GetValue<string>()=="ready","Reinstalled DLL failed initialization");await Send("status",0);await Send("stop",0);await host.StopOfflineAsync();
 await store.SetEnabledAsync(installed.Id,false);await store.RemoveAsync(installed.Id);Require(store.Modules().Count==0,"Actual module removal left a module record");
 await host.StartOfflineAsync(executable);var empty=await host.RequestAsync("status");Require(empty["modules"]?.AsArray().Count==0,"Removed module remained in a new host");await host.StopOfflineAsync();
 result["installation_lifecycle"]=new JsonObject{["disable_enable"]=true,["reimport_preserved_configuration"]=true,["reimport_native_initialized"]=true,["remove_empty_host"]=true};result["passed"]=true;
 result["limitations"]=new JsonArray("DLL initialization, EML import and offline message/configuration lifecycle only","No live Unity binding, game animation, special sword rendering or fire effect tested","Demo/toggle correctly returned BE_Result_NotReady (2) without a Unity runtime");
 Console.WriteLine("EML isolated import and actual OfflineHost: six mode loads/PCM, config roundtrip, invalid mode rollback, NotReady, upgrade/reinstall/disable/remove and shutdown passed");
}catch(Exception error){result["passed"]=false;result["error_type"]=error.GetType().Name;result["error"]=error.Message;Console.Error.WriteLine("EML verification failed: "+error.GetType().Name+": "+error.Message);Environment.ExitCode=1;}
finally{
 await File.WriteAllTextAsync(Path.Combine(run,"result.json"),result.ToJsonString(new JsonSerializerOptions{WriteIndented=true}));
 lock(log)File.WriteAllLines(Path.Combine(run,"offline-host.log"),log);
}
