#define _POSIX_C_SOURCE 200809L
#include "interface/UiOffspringApp.h"
#include "interface/UiRecipeCatalog.h"
#include "interface/UiRecipeRoutes.h"
#include "interface/UiTheme.h"
#include "UiCreatureScene.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct UiOffspringJob {
    pthread_t thread;
    atomic_bool cancel, done;
    atomic_size_t completed;
    const char *directory;
    const UiVfsNode *parents[2];
    CreatureOffspringConfig config;
    const CreatureOffspringBatch *source;
    CreatureOffspringBatch *result;
    OffspringCandidate *replacement;
    size_t index;
    char error[256];
    double milliseconds;
};

static bool Cancelled(void *state) { return atomic_load(&((UiOffspringJob *)state)->cancel); }

static void Progress(void *state, size_t completed)
{
    atomic_store(&((UiOffspringJob *)state)->completed,completed);
}

static void *Generate(void *state)
{
    UiOffspringJob *job = state;
    struct timespec start = {0}, end = {0};
    clock_gettime(CLOCK_MONOTONIC,&start);
    if (job->source) {
        job->replacement = CreatureOffspring_PrepareReroll(job->source,job->index,
            Cancelled,job,job->error,sizeof(job->error));
    } else {
        CreatureRecipeDocument *parents = malloc(2*sizeof(*parents));
        if (!parents) snprintf(job->error,sizeof(job->error),"Cannot allocate parent recipes");
        else if (UiCreatureScene_LoadRecipe(job->directory,job->parents[0],&parents[0],
                     job->error,sizeof(job->error)) && !Cancelled(job)) {
            const CreatureRecipe *first = &parents[0].recipe, *second = first;
            if (job->parents[0] != job->parents[1]) {
                second = UiCreatureScene_LoadRecipe(job->directory,job->parents[1],&parents[1],
                    job->error,sizeof(job->error)) ? &parents[1].recipe : NULL;
            }
            if (second) job->result = CreatureOffspring_GenerateProgress(first,second,job->config,
                Cancelled,Progress,job,job->error,sizeof(job->error));
        }
        free(parents);
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    job->milliseconds = (end.tv_sec-start.tv_sec)*1000.0 + (end.tv_nsec-start.tv_nsec)/1000000.0;
    atomic_store(&job->done,true);
    return NULL;
}

static void Notify(UiOffspringApp *app, bool error, const char *text)
{
    snprintf(app->status,sizeof(app->status),"%.*s",(int)sizeof(app->status)-1,text);
    app->error = error;
    if (app->terminal) UiTerminal_Append(app->terminal,error ? UI_LOG_ERROR : UI_LOG_OUTPUT,text);
}

static void Finish(UiOffspringApp *app, bool cancel)
{
    UiOffspringJob *job = app->job;
    if (!job) return;
    if (cancel) atomic_store(&job->cancel,true);
    pthread_join(job->thread,NULL);
    app->job = NULL;
    if (!cancel) {
        char message[256];
        if (job->result) {
            UiCreatureVisualizer_InvalidateTemporary(app->visualizer,0);
            CreatureOffspring_Destroy(app->batch);
            app->batch = job->result;
            job->result = NULL;
            memset(app->saved,0,sizeof(app->saved));
            app->selected = app->scroll = 0;
            snprintf(message,sizeof(message),"Generated %zu candidates · seed %" PRIu32 " · %.1f ms",
                CreatureOffspring_Count(app->batch),CreatureOffspring_Seed(app->batch),job->milliseconds);
            Notify(app,false,message);
        } else if (job->replacement && CreatureOffspring_AcceptReroll(app->batch,job->index,job->replacement)) {
            UiCreatureVisualizer_InvalidateTemporary(app->visualizer,(unsigned)job->index+1);
            job->replacement = NULL;
            app->saved[job->index] = 0;
            const OffspringReport *report;
            CreatureOffspring_Get(app->batch,job->index,&report);
            snprintf(message,sizeof(message),"Rerolled #%zu · seed %" PRIu32 " · %.1f ms",
                job->index+1,report->seed,job->milliseconds);
            Notify(app,false,message);
        } else Notify(app,true,*job->error ? job->error : "Generation cancelled");
    }
    CreatureOffspring_Destroy(job->result);
    CreatureOffspring_DiscardReroll(job->replacement);
    free(job);
}

static void Clear(UiOffspringApp *app)
{
    Finish(app,true);
    UiCreatureVisualizer_InvalidateTemporary(app->visualizer,0);
    CreatureOffspring_Destroy(app->batch);
    app->batch = NULL;
    app->selected = app->scroll = 0;
    memset(app->saved,0,sizeof(app->saved));
    *app->status = 0;
    app->error = false;
}

static void Close(void *state)
{
    UiOffspringApp *app = state;
    app->window = 0;
    Clear(app);
}

static bool IsOpen(const UiOffspringApp *app)
{
    const UiWindow *window = UiDesktop_Find(app->desktop,app->window);
    return window && window->motion != UI_WINDOW_CLOSING;
}

bool UiOffspringApp_NeedsFrame(const UiOffspringApp *app) { return app->job != NULL; }

void UiOffspringApp_Update(UiOffspringApp *app)
{
    if (app->window && !IsOpen(app)) Clear(app);
    else if (app->job && atomic_load(&app->job->done)) Finish(app,false);
}

static bool WriteRecipe(FILE *file, uint32_t id, void *context)
{
    CreatureRecipeDocument *doc = context;
    doc->recipe.id = id;
    if (!CreatureRecipeIO_WriteJSON(file,&doc->recipe) || fflush(file) != 0) return false;
    long bytes = ftell(file);
    if (bytes <= 0 || bytes > 2*1024*1024 || fseek(file,0,SEEK_SET) != 0) return false;
    char *json = malloc((size_t)bytes+1);
    if (!json) return false;
    bool ok = fread(json,1,(size_t)bytes,file) == (size_t)bytes;
    json[bytes] = 0;
    if (ok) ok = CreatureRecipeIO_ReadJSON(doc,json,NULL,0);
    free(json);
    return ok;
}

static bool SafeName(const char *name)
{
    return *name && strlen(name) <= 128 &&
        name[strspn(name,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")] == 0;
}

static void Slug(char out[33], const char *name)
{
    size_t n = 0;
    for (size_t i=0; name[i] && n<32; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
        else if (n && out[n-1] != '-') out[n++] = '-';
    }
    if (n && out[n-1] == '-') --n;
    if (!n) { strcpy(out,"parent"); return; }
    out[n] = 0;
}

static bool Save(UiOffspringApp *app, size_t index, const char *name, char *out, size_t capacity)
{
    if (app->saved[index]) return UiVfs_Path(app->fs,app->saved[index],out,capacity);
    const OffspringReport *report;
    const CreatureRecipe *candidate = CreatureOffspring_Get(app->batch,index,&report);
    if (!candidate) { snprintf(out,capacity,"Candidate does not exist"); return false; }
    char stem[129], filename[UI_VFS_NAME_BYTES], detail[256] = "";
    if (name) {
        if (!SafeName(name)) {
            snprintf(out,capacity,"Use a filename with 1–128 letters, digits, hyphens or underscores");
            return false;
        }
        strcpy(stem,name);
    } else {
        char a[33], b[33];
        Slug(a,CreatureOffspring_Parent(app->batch,0)->name);
        Slug(b,CreatureOffspring_Parent(app->batch,1)->name);
        snprintf(stem,sizeof(stem),"%s-%s-offspring-%zu-%08" PRIx32,a,b,index+1,report->seed);
    }

    CreatureRecipeDocument *doc = malloc(sizeof(*doc));
    if (!doc) { snprintf(out,capacity,"Cannot allocate recipe for saving"); return false; }
    CreatureRecipeDocument_Copy(doc,candidate);
    if (name) strcpy(doc->name,name);
    UiVfsId saved = 0;
    for (unsigned attempt=0; attempt<128; ++attempt) {
        if (attempt) snprintf(filename,sizeof(filename),"%s-%u.creature.json",stem,attempt+1);
        else snprintf(filename,sizeof(filename),"%s.creature.json",stem);
        if (!name && UiVfs_FindChild(app->fs,app->folder,filename)) continue;
        saved = UiRecipeCatalog_SaveProject(app->fs,app->folder,app->visualizer->executableDirectory,
            filename,candidate->id,WriteRecipe,doc,detail,sizeof(detail));
        if (saved || name || strcmp(detail,"Recipe filename already exists or cannot be checked")) break;
    }
    free(doc);
    if (!saved) {
        snprintf(out,capacity,"%s",*detail ? detail : "No unique filename available");
        return false;
    }
    app->saved[index] = saved;
    if (*detail) Notify(app,true,detail);
    return UiVfs_Path(app->fs,saved,out,capacity);
}

static bool Preview(UiOffspringApp *app, size_t index, char *out, size_t capacity)
{
    if (app->saved[index])
        return UiCreatureVisualizer_ViewRecipe(app->visualizer,app->saved[index],out,capacity);
    return UiCreatureVisualizer_ViewTemporary(app->visualizer,
        CreatureOffspring_Get(app->batch,index,NULL),(unsigned)index+1,out,capacity);
}

static UiRect List(UiRect box) { return (UiRect){box.x+12,box.y+91,box.w-24,box.h-175}; }
static size_t Rows(UiRect box) { return box.h>175 ? (size_t)(box.h-175)/24 : 0; }
static size_t ScrollLimit(UiRect box, size_t count)
{
    size_t rows = Rows(box);
    return count > rows ? count-rows : 0;
}
static UiRect Button(UiRect box, unsigned i) { return (UiRect){box.x+12+101*(int)i,box.y+box.h-34,92,25}; }

static void Draw(void *state, UiRenderer *r, UiRect box, bool active, bool caret)
{
    (void)active; (void)caret;
    UiOffspringApp *app = state;
    if (!UiRenderer_PushClip(r,box)) return;
    UiRenderer_Rect(r,box,UI_INK);
    size_t count = CreatureOffspring_Count(app->batch);
    char line[256];
    const CreatureRecipe *a = CreatureOffspring_Parent(app->batch,0);
    const CreatureRecipe *b = CreatureOffspring_Parent(app->batch,1);
    if (count) snprintf(line,sizeof(line),"%.44s × %.44s",a->name,b->name);
    else snprintf(line,sizeof(line),"No active generation");
    UiRenderer_Text(r,box.x+12,box.y+12,17,UI_PAPER,line);
    if (count) {
        snprintf(line,sizeof(line),"%zu candidates  ·  Seed %" PRIu32,count,CreatureOffspring_Seed(app->batch));
        UiRenderer_Text(r,box.x+12,box.y+38,13,UI_ACCENT,line);
    } else {
        UiRenderer_Text(r,box.x+12,box.y+38,12,UI_MUTED,
            "CO.create.5.\"parent-a.creature.json\".\"parent-b.creature.json\"");
    }
    if (app->job) {
        UiOffspringJob *job = app->job;
        if (job->source) snprintf(line,sizeof(line),"Rerolling candidate #%zu…",job->index+1);
        else snprintf(line,sizeof(line),"Generating %zu/%zu  ·  Seed %" PRIu32,
            atomic_load(&job->completed),job->config.count,job->config.seed);
    } else snprintf(line,sizeof(line),"%s",app->status);
    UiRenderer_Text(r,box.x+12,box.y+62,12,app->error && !app->job ? UI_ERROR : UI_MUTED,line);

    UiRect list = List(box);
    size_t limit = ScrollLimit(box,count);
    if (app->scroll > limit) app->scroll = limit;
    UiRenderer_Line(r,list.x,list.y-5,list.x+list.w,list.y-5,1,UI_EDGE);
    for (size_t at=0; at<Rows(box) && at+app->scroll<count; ++at) {
        size_t index = at+app->scroll;
        const CreatureRecipe *candidate = CreatureOffspring_Get(app->batch,index,NULL);
        int y = list.y+(int)at*24;
        if (index == app->selected) UiRenderer_Rect(r,(UiRect){list.x,y,list.w,23},0x193644ff);
        snprintf(line,sizeof(line),"%2zu   %.45s",index+1,candidate->name);
        UiRenderer_Text(r,list.x+5,y+4,13,UI_PAPER,line);
        UiRenderer_Text(r,list.x+list.w-71,y+5,11,app->saved[index] ? UI_ACCENT : UI_MUTED,
            app->saved[index] ? "Saved" : "Unsaved");
    }
    if (limit && Rows(box)) {
        int height = (int)Rows(box)*24;
        int thumb = height*(int)Rows(box)/(int)count;
        int y = list.y+(height-thumb)*(int)app->scroll/(int)limit;
        UiRenderer_Rect(r,(UiRect){list.x+list.w-3,list.y,2,height},UI_EDGE);
        UiRenderer_Rect(r,(UiRect){list.x+list.w-3,y,2,thumb},UI_ACCENT);
    }
    if (count) {
        const OffspringReport *report;
        CreatureOffspring_Get(app->batch,app->selected,&report);
        snprintf(line,sizeof(line),"Head blend %.0f%% Y  ·  Tail %.24s",100*report->resolvedHeadWeight,
            CreatureOffspring_Parent(app->batch,report->tailParent)->name);
        UiRenderer_Text(r,box.x+12,box.y+box.h-70,12,UI_ACCENT,line);
        snprintf(line,sizeof(line),"Validated  ·  %u grafts  ·  %u repair passes",
            report->grafts,report->repairs);
        UiRenderer_Text(r,box.x+12,box.y+box.h-53,12,UI_MUTED,line);
        const char *labels[] = {"Preview","Save"};
        for (unsigned i=0; i<2; ++i) {
            UiRect button = Button(box,i);
            UiRenderer_Outline(r,button,UI_EDGE);
            UiRenderer_Text(r,button.x+9,button.y+5,13,UI_PAPER,labels[i]);
        }
    }
    UiRenderer_PopClip(r);
}

static void Input(void *state, UiRect box, const UiEvent *event)
{
    UiOffspringApp *app = state;
    size_t count = CreatureOffspring_Count(app->batch);
    if (!count) return;
    size_t rows = Rows(box);
    size_t maxScroll = ScrollLimit(box,count);
    if (event->type == UI_WHEEL) {
        int offset = (int)app->scroll-event->wheel;
        app->scroll = offset<0 ? 0 : (size_t)offset>maxScroll ? maxScroll : (size_t)offset;
        return;
    }
    if (event->type != UI_PRESS) return;
    char out[UI_VFS_PATH_BYTES];
    UiRect list = List(box);
    if (UiRect_Contains(list,event->x,event->y)) {
        size_t row = (size_t)(event->y-list.y)/24, index = app->scroll+row;
        if (row >= rows || index >= count) return;
        app->selected = index;
        if (event->clicks >= 2 && !Preview(app,index,out,sizeof(out))) Notify(app,true,out);
    } else if (UiRect_Contains(Button(box,0),event->x,event->y)) {
        if (!Preview(app,app->selected,out,sizeof(out))) Notify(app,true,out);
    } else if (UiRect_Contains(Button(box,1),event->x,event->y)) {
        bool saved = Save(app,app->selected,NULL,out,sizeof(out));
        Notify(app,!saved,out);
    }
}

void UiOffspringApp_Icon(UiRenderer *r, int x, int y, uint32_t color)
{
    UiRenderer_Line(r,x+2,y+1,x+7,y+5,1,color);
    UiRenderer_Line(r,x+12,y+1,x+7,y+5,1,color);
    UiRenderer_Line(r,x+7,y+5,x+7,y+10,1,color);
    UiRenderer_Rect(r,(UiRect){x+1,y,3,3},color);
    UiRenderer_Rect(r,(UiRect){x+11,y,3,3},color);
    UiRenderer_Rect(r,(UiRect){x+6,y+8,3,3},color);
}

bool UiOffspringApp_Open(UiOffspringApp *app)
{
    if (IsOpen(app)) {
        UiDesktop_Activate(app->desktop,app->window);
        return true;
    }
    if (app->window) UiDesktop_CloseImmediate(app->desktop,app->window);
    UiRect area = app->desktop->area;
    app->window = UiDesktop_Add(app->desktop,(UiWindow){
        .rect={area.x+30,area.y+35,560,410},.minWidth=400,.minHeight=260,
        .title="CreatureOffspring",.state=app,.draw=Draw,.drawIcon=UiOffspringApp_Icon,.input=Input,.onClose=Close
    });
    return app->window != 0;
}

static bool Number(const char **args, uint32_t *number)
{
    const char *pos = *args;
    if (*pos<'0' || *pos>'9') return false;
    uint32_t result = 0;
    while (*pos>='0' && *pos<='9') {
        unsigned digit = (unsigned)(*pos++-'0');
        if (result > (UINT32_MAX-digit)/10) return false;
        result = result*10+digit;
    }
    *args = pos;
    *number = result;
    return true;
}

static bool Index(UiOffspringApp *app, const char **args, uint32_t *index, char *out, size_t capacity)
{
    const char *error = NULL;
    if (!Number(args,index) || !*index) error = "Expected a positive candidate number";
    else if (!IsOpen(app)) error = "CreatureOffspring is closed";
    else if (!app->batch) error = "No active offspring generation";
    else if (*index>CreatureOffspring_Count(app->batch)) error = "Candidate does not exist";
    if (error) snprintf(out,capacity,"%s",error);
    return !error;
}

static bool Start(UiOffspringApp *app, UiOffspringJob *job, char *out, size_t capacity)
{
    atomic_init(&job->cancel,false);
    atomic_init(&job->done,false);
    atomic_init(&job->completed,0);
    if (pthread_create(&job->thread,NULL,Generate,job) != 0) {
        free(job);
        snprintf(out,capacity,"Cannot start the generation worker");
        return false;
    }
    app->job = job;
    return true;
}

static UiCommandResult Create(UiOffspringApp *app, const char *args, char *out, size_t capacity)
{
    uint32_t count, seed;
    if (!Number(&args,&count) || *args != '.' || !count || count > CREATURE_OFFSPRING_MAX) {
        snprintf(out,capacity,"Expected 1–%d offspring followed by two quoted recipe paths",CREATURE_OFFSPRING_MAX);
        return UI_COMMAND_ERROR;
    }
    const UiShell *shell = app->visualizer->shell;
    const UiVfsNode *a, *b;
    const char *suffix;
    const char *error = UiRecipeRoutes_ResolveWithSuffix(shell->fs,shell->cwd,args+1,true,&a,&b,&suffix);
    if (error) { snprintf(out,capacity,"%s",error); return UI_COMMAND_ERROR; }
    if (*suffix) {
        if (strncmp(suffix,".seed.",6) != 0) {
            snprintf(out,capacity,"Expected optional .seed.<unsigned number> after recipe paths");
            return UI_COMMAND_ERROR;
        }
        suffix += 6;
        if (!Number(&suffix,&seed) || *suffix) {
            snprintf(out,capacity,"Invalid seed (expected 0–4294967295)");
            return UI_COMMAND_ERROR;
        }
    } else {
        struct timespec now = {0};
        clock_gettime(CLOCK_REALTIME,&now);
        seed = (uint32_t)now.tv_sec ^ (uint32_t)now.tv_nsec ^ ++app->seedNonce*UINT32_C(0x9e3779b9);
    }
    if (!UiOffspringApp_Open(app)) {
        snprintf(out,capacity,"Desktop window limit reached");
        return UI_COMMAND_ERROR;
    }
    if (app->job) {
        snprintf(out,capacity,"Generation already in progress");
        return UI_COMMAND_ERROR;
    }
    UiOffspringJob *job = calloc(1,sizeof(*job));
    if (!job) { snprintf(out,capacity,"Cannot allocate generation request"); return UI_COMMAND_ERROR; }
    job->config = CreatureOffspring_Default(count,seed);
    job->parents[0] = a;
    job->parents[1] = b;
    job->directory = app->visualizer->executableDirectory;
    if (!Start(app,job,out,capacity)) return UI_COMMAND_ERROR;
    snprintf(out,capacity,"Generating %" PRIu32 " candidates · seed %" PRIu32,count,seed);
    return UI_COMMAND_OK;
}

static void Info(UiOffspringApp *app, size_t index, char *out, size_t capacity)
{
    const OffspringReport *r;
    const CreatureRecipe *recipe = CreatureOffspring_Get(app->batch,index,&r);
    char path[UI_VFS_PATH_BYTES] = "Unsaved";
    if (app->saved[index]) UiVfs_Path(app->fs,app->saved[index],path,sizeof(path));
    const CreaturePhenotype *p = &recipe->adult;
    char fit[192];
    if (r->hasMorphTarget) {
        snprintf(fit,sizeof(fit),"Morph reference %.1f%% · geometric weight %.1f%% · head basis Y %.1f%%\n"
            "Normalized error  head %.3f  body %.3f  limbs %.3f · %u fit evaluations%s",
            100*r->global,100*r->targetWeight,100*r->resolvedHeadWeight,
            r->headError,r->bodyError,r->limbError,r->fitEvaluations,
            r->simplifiedHeadBasis ? " · simplified inherited head" : "");
    } else strcpy(fit,"Resolved morph reference unavailable");
    snprintf(out,capacity,
        "%s\nBatch seed %" PRIu32 " · candidate seed %" PRIu32 "\n"
        "Parent Y weights  global %.0f%%  body %.0f%%  head %.0f%%  limbs %.0f%%  tail %.0f%%  surface %.0f%%\n"
        "%s\n"
        "Structural systems X/Y  scaffold %c  head slots %c  foot topology %c  tail %c  material seed %c\n"
        "Adult  %zu limbs  %zu tails  %zu ornaments  %zu flexible appendages  ·  %u grafts  ·  %u omitted modules or anchors\n"
        "Omissions  %u unsupported  %u capacity or occupied slot  %u unselected  %u surface anchors\n"
        "Validated 4 stages and 3 intermediate ages  ·  %u attempts  ·  %u repair passes\n%s",
        recipe->name,CreatureOffspring_Seed(app->batch),r->seed,
        100*r->global,100*r->body,100*r->head,100*r->limbs,100*r->tail,100*r->surface,
        fit,'X'+r->scaffoldParent,'X'+r->headParent,'X'+r->limbParent,'X'+r->tailParent,'X'+r->surfaceParent,
        p->limbCount,p->tailCount,p->ornamentCount,p->flexibleAppendageCount,r->grafts,r->omitted,
        r->unsupportedGrafts,r->constrainedGrafts,r->unselectedGrafts,r->removedAnchors,r->attempts,r->repairs,path);
}

static UiCommandResult ViewCommand(void *state, const char *args, char *out, size_t capacity)
{
    UiOffspringApp *app = state;
    uint32_t index;
    if (!Index(app,&args,&index,out,capacity)) return UI_COMMAND_ERROR;
    if (*args) { snprintf(out,capacity,"Unexpected text after candidate number"); return UI_COMMAND_ERROR; }
    return Preview(app,index-1,out,capacity) ? UI_COMMAND_SILENT : UI_COMMAND_ERROR;
}

static UiCommandResult OpenCommand(void *state, const char *args, char *out, size_t capacity)
{
    if (!*args && UiOffspringApp_Open(state)) return UI_COMMAND_SILENT;
    snprintf(out,capacity,"%s",*args ? "Use CO.help for commands" : "Desktop window limit reached");
    return UI_COMMAND_ERROR;
}

static UiCommandResult Command(void *state, const char *args, char *out, size_t capacity)
{
    UiOffspringApp *app = state;
    if (!strcmp(args,"help")) {
        snprintf(out,capacity,
            "CO or CreatureOffspring\n"
            "CO.create.5.\"route1\".\"route2\"[.seed.48216]\n"
            "CO.list   CO.info.4   CV.view.CO.4\n"
            "CO.save.4[.\"CustomName\"]   CO.reroll.4\n"
            "CO.clear   CO.close   CO.help");
        return UI_COMMAND_OK;
    }
    if (!strncmp(args,"create.",7)) return Create(app,args+7,out,capacity);
    if (!strcmp(args,"close")) {
        if (IsOpen(app)) {
            Clear(app);
            UiDesktop_Close(app->desktop,app->window);
        }
        return UI_COMMAND_SILENT;
    }
    if (!strcmp(args,"clear")) {
        if (!IsOpen(app)) { snprintf(out,capacity,"CreatureOffspring is closed"); return UI_COMMAND_ERROR; }
        Clear(app);
        snprintf(out,capacity,"Temporary generation cleared");
        return UI_COMMAND_OK;
    }
    if (!strcmp(args,"list")) {
        if (!IsOpen(app)) { snprintf(out,capacity,"CreatureOffspring is closed"); return UI_COMMAND_ERROR; }
        size_t count = CreatureOffspring_Count(app->batch), bytes = 0;
        if (!count) snprintf(out,capacity,"No active offspring generation");
        for (size_t i=0; i<count && bytes<capacity; ++i) {
            const CreatureRecipe *r = CreatureOffspring_Get(app->batch,i,NULL);
            int n = snprintf(out+bytes,capacity-bytes,"%s%2zu %.37s  Valid  %s",
                i ? "\n" : "",i+1,r->name,app->saved[i] ? "Saved" : "Unsaved");
            if (n < 0) break;
            bytes += (size_t)n;
        }
        return UI_COMMAND_OK;
    }
    const char *prefix = !strncmp(args,"info.",5) ? "info." :
        !strncmp(args,"save.",5) ? "save." : !strncmp(args,"reroll.",7) ? "reroll." : NULL;
    if (!prefix) { snprintf(out,capacity,"Invalid CreatureOffspring command. See CO.help"); return UI_COMMAND_ERROR; }
    args += strlen(prefix);
    uint32_t index;
    if (!Index(app,&args,&index,out,capacity)) return UI_COMMAND_ERROR;
    --index;
    if (prefix[0] == 's') {
        char name[UI_VFS_PATH_BYTES];
        if (*args == '.') {
            ++args;
            const char *error = UiRecipeRoutes_ReadQuoted(&args,name);
            if (error) { snprintf(out,capacity,"Expected a double-quoted custom filename"); return UI_COMMAND_ERROR; }
            if (!SafeName(name)) { snprintf(out,capacity,"Invalid custom filename"); return UI_COMMAND_ERROR; }
        } else *name = 0;
        if (!*args) return Save(app,index,*name ? name : NULL,out,capacity) ? UI_COMMAND_OK : UI_COMMAND_ERROR;
    }
    if (*args) { snprintf(out,capacity,"Unexpected text after command arguments"); return UI_COMMAND_ERROR; }
    if (prefix[0] == 'i') {
        Info(app,index,out,capacity);
        return UI_COMMAND_OK;
    }
    if (app->job) { snprintf(out,capacity,"Generation already in progress"); return UI_COMMAND_ERROR; }
    UiOffspringJob *job = calloc(1,sizeof(*job));
    if (!job) { snprintf(out,capacity,"Cannot allocate reroll request"); return UI_COMMAND_ERROR; }
    job->source = app->batch;
    job->index = index;
    if (!Start(app,job,out,capacity)) return UI_COMMAND_ERROR;
    snprintf(out,capacity,"Rerolling #%u",index+1);
    return UI_COMMAND_OK;
}

bool UiOffspringApp_Register(UiOffspringApp *app, UiCommands *registry)
{
    return UiCommands_Register(registry,"CO",OpenCommand,app) &&
        UiCommands_Register(registry,"CreatureOffspring",OpenCommand,app) &&
        UiCommands_RegisterPrefix(registry,"CO.",Command,app) &&
        UiCommands_RegisterPrefix(registry,"CreatureOffspring.",Command,app) &&
        UiCommands_RegisterPrefix(registry,"CV.view.CO.",ViewCommand,app) &&
        UiCommands_RegisterPrefix(registry,"CreatureVisualizer.view.CO.",ViewCommand,app);
}
