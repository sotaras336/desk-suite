#pragma once
#include "app.h"

// Local search for Start: calculator, units, Run box, apps, classic programs, Settings pages, folders, files (own index). No web.
enum class RKind { App, Setting, File, Folder, Calc, Run, Program };
struct Result { RKind kind; ws title, subtitle, target; double score; ws appId; };

ws Norm(const ws& s);                               // lower case, accents/punctuation removed
double Score(const ws& text, const ws& query);      // 0 = no match
ws Shorten(const ws& path);                         // C:\Users\me\x -> ~\x
ws Ago(FILETIME when);
std::vector<Result> SearchLocal(const ws& query);   // apps, settings, calc, run box, folders (fast; UI thread)
void SearchFiles(const ws& query, std::function<void(std::vector<Result>)> cb);   // own file index, latest request wins; cb on the UI thread
double HabitBoost(const ws& query, RKind kind, const ws& target);
void HabitRecord(const ws& query, RKind kind, const ws& target);
void RecordLaunch(const ws& appId);
ws ExpandPath(const ws& q);
ws ResolveCommand(const ws& cmd);

struct CatApp { ws name, id; wchar_t Initial() const { return !name.empty() && iswalpha(name[0]) ? (wchar_t)towupper(name[0]) : L'#'; } };
void CatalogLoad();                                 // from apps.json (fast)
void CatalogRefresh(std::function<void()> onUpdated);   // re-read shell:AppsFolder on a worker
const std::vector<CatApp>& CatalogApps();
const CatApp* CatalogFindByName(const ws& name);
const CatApp* CatalogFindById(const ws& id);

struct RecentItem { ws path, name, appName, ext; FILETIME when; };
std::vector<RecentItem> RecentLoad(int max);        // blocking: call from a worker
std::vector<RecentItem> RecentCached();             // refreshed in the background every 30 s; never blocks
ws FriendlyAppFor(const ws& ext);
ws ExecutableFor(const ws& ext);

void ProgramsLoadAsync();
void FileIndexStart();
bool FileIndexSelfTest(ws& detail);
int FileIndexBuild();                        // --buildindex: scans the drives and saves fileindex.txt (its own process, started by the bar)
bool FileIndexScanSelfTest(ws& detail);   // scans a small temp folder and saves it the way a rescan does
void FileIndexQuery(const ws& query, const ws& file);   // --indexquery "words" file: one search on the saved index, best matches written to the file
void FileIndexMemReport(const ws& file);   // --indexmem: how much memory the saved index takes once loaded   // builds a tiny index in memory and checks that full paths and depths come back exactly
