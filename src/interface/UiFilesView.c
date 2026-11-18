#include "interface/UiFilesView.h"
#include "interface/UiTheme.h"
#include <stdio.h>
#include <string.h>

enum { PAD = 10, ROW = 25, TOOLBAR = 44, FOOTER = 31, HEADER = 26 };

typedef struct {
    UiRect back, up, path, sidebar, root, recipes, header, rows, footer;
    int visible, sourceX;
} Layout;

static Layout Geometry(UiRect c)
{
    int sidebar = c.w >= 430 ? 116 : 98;
    int x = c.x+sidebar, width = c.w-sidebar;
    int footerY = c.y+c.h-FOOTER, bodyY = c.y+TOOLBAR;
    Layout l = {
        .back={c.x+PAD,c.y+9,56,26}, .up={c.x+72,c.y+9,43,26},
        .path={c.x+121,c.y+9,c.w-121-PAD,26},
        .sidebar={c.x+1,bodyY,sidebar-1,footerY-bodyY},
        .root={c.x+7,bodyY+28,sidebar-14,26},
        .recipes={c.x+7,bodyY+56,sidebar-14,26},
        .header={x,bodyY,width,HEADER},
        .rows={x,bodyY+HEADER,width,footerY-bodyY-HEADER},
        .footer={c.x+1,footerY,c.w-2,FOOTER-1},
        .sourceX=c.w >= 460 ? c.x+c.w-86 : 0
    };
    l.visible = l.rows.h/ROW;
    if (l.visible < 1) l.visible = 1;
    l.rows.h = l.visible*ROW;
    return l;
}

void UiFilesView_Icon(UiRenderer *r, int x, int y, uint32_t color)
{
    UiRenderer_Outline(r,(UiRect){x,y+2,14,9},color);
    UiRenderer_Rect(r,(UiRect){x+1,y,6,2},color);
    UiRenderer_Rect(r,(UiRect){x+2,y+4,10,1},color);
}

static void FileIcon(UiRenderer *r, int x, int y, UiColor c)
{
    UiRenderer_Outline(r,(UiRect){x+2,y,10,13},c);
    UiRenderer_Rect(r,(UiRect){x+4,y+5,6,1},c);
    UiRenderer_Rect(r,(UiRect){x+4,y+8,4,1},c);
}

static void Button(UiRenderer *r, UiRect b, const char *label, bool enabled, bool up)
{
    UiColor color = enabled ? UI_PAPER : UI_MUTED;
    UiColor icon = enabled ? UI_ACCENT : UI_MUTED;
    UiRenderer_Gradient(r,b,enabled ? 0x183747ff : 0x112431ff,0x0c1c29ff);
    UiRenderer_Outline(r,b,enabled ? UI_EDGE : 0x233b4bff);
    int x = b.x+10, y = b.y+13;
    if (up) {
        UiRenderer_Line(r,x,y+4,x,y-4,1,icon);
        UiRenderer_Line(r,x-3,y-1,x,y-4,1,icon);
        UiRenderer_Line(r,x,y-4,x+3,y-1,1,icon);
    } else {
        UiRenderer_Line(r,x,y,x+7,y,1,icon);
        UiRenderer_Line(r,x+3,y-3,x,y,1,icon);
        UiRenderer_Line(r,x,y,x+3,y+3,1,icon);
    }
    UiRenderer_Text(r,b.x+20,b.y+5,12,color,label);
}

static void Place(UiRenderer *r, UiRect b, const char *name, bool selected)
{
    UiColor color = selected ? UI_ACCENT : UI_MUTED;
    if (selected) {
        UiRenderer_Rect(r,b,0x17404aff);
        UiRenderer_Rect(r,(UiRect){b.x,b.y,2,b.h},UI_ACCENT);
    }
    UiFilesView_Icon(r,b.x+7,b.y+7,color);
    UiRenderer_Text(r,b.x+26,b.y+4,12,selected ? UI_PAPER : UI_MUTED,name);
}

static const char *Source(const UiVfsNode *node)
{
    if (node->type == UI_VFS_DIRECTORY) return "Folder";
    if (node->source == UI_VFS_EXTERNAL_RECIPE) return "External";
    if (node->source == UI_VFS_BUILTIN_RECIPE) return "Built-in";
    return "Virtual";
}

static void DrawItems(UiRenderer *r, const UiFilesApp *app, Layout l, int count)
{
    UiRenderer_Rect(r,l.header,0x102534ff);
    UiRenderer_Text(r,l.header.x+12,l.header.y+4,11,UI_MUTED,"NAME");
    if (l.sourceX) UiRenderer_Text(r,l.sourceX,l.header.y+4,11,UI_MUTED,"SOURCE");
    UiRenderer_Rect(r,(UiRect){l.header.x,l.rows.y-1,l.rows.w,1},UI_EDGE);
    if (!UiRenderer_PushClip(r,l.rows)) return;

    if (!count) UiRenderer_Text(r,l.rows.x+12,l.rows.y+11,13,UI_MUTED,"Empty folder");
    UiVfsId id = UiFilesApp_Item(app,app->firstRow);
    for (int row=0; id && row<l.visible; ++row, id=UiVfs_NextSibling(app->fs,id)) {
        const UiVfsNode *node = UiVfs_Get(app->fs,id);
        int x = l.rows.x, y = l.rows.y+row*ROW;
        bool selected = id == app->selected;
        if (selected) {
            UiRenderer_Rect(r,(UiRect){x+1,y,l.rows.w-10,ROW},0x1b4853ff);
            UiRenderer_Rect(r,(UiRect){x+1,y,2,ROW},UI_ACCENT);
        } else if (row%2 == 0) UiRenderer_Rect(r,(UiRect){x,y,l.rows.w,ROW},0x0b1b28ff);
        if (node->type == UI_VFS_DIRECTORY) UiFilesView_Icon(r,x+12,y+7,UI_ACCENT);
        else FileIcon(r,x+12,y+6,selected ? UI_ACCENT : UI_MUTED);
        int textX = x+34, endX = l.sourceX ? l.sourceX-8 : x+l.rows.w-12;
        if (UiRenderer_PushClip(r,(UiRect){textX,y,endX-textX,ROW})) {
            UiRenderer_Text(r,textX,y+4,13,UI_PAPER,node->name);
            UiRenderer_PopClip(r);
        }
        if (l.sourceX) UiRenderer_Text(r,l.sourceX,y+5,12,UI_MUTED,Source(node));
    }
    if (count > l.visible) {
        int size = l.rows.h*l.visible/count;
        if (size < 8) size = 8;
        int y = (l.rows.h-size)*app->firstRow/(count-l.visible);
        int x = l.rows.x+l.rows.w-6;
        UiRenderer_Rect(r,(UiRect){x,l.rows.y,2,l.rows.h},0x203c4cff);
        UiRenderer_Rect(r,(UiRect){x,l.rows.y+y,2,size},0x64adbaff);
    }
    UiRenderer_PopClip(r);
}

void UiFilesView_Draw(void *state, UiRenderer *r, UiRect c, bool active, bool caret)
{
    (void)caret;
    UiFilesApp *app = state;
    Layout l = Geometry(c);
    int count = UiFilesApp_Count(app);
    UiVfsId root = UiVfs_Root(app->fs), recipes = UiVfs_FindChild(app->fs,root,"recepies");
    UiFilesApp_Scroll(app,0,l.visible);
    UiRenderer_Rect(r,c,UI_INK);
    UiRenderer_Outline(r,c,0x284352ff);
    UiRenderer_Gradient(r,(UiRect){c.x+1,c.y+1,c.w-2,TOOLBAR-1},0x122d3dff,0x0b1f2dff);
    Button(r,l.back,"Back",app->backCount != 0,false);
    Button(r,l.up,"Up",app->directory != root,true);
    UiRenderer_Rect(r,l.path,0x081a26ff);
    UiRenderer_Outline(r,l.path,UI_EDGE);
    UiRenderer_Rect(r,(UiRect){l.path.x+9,l.path.y+11,4,4},active ? UI_ACCENT : UI_MUTED);
    char path[UI_VFS_PATH_BYTES];
    if (UiVfs_Path(app->fs,app->directory,path,sizeof(path)) &&
        UiRenderer_PushClip(r,(UiRect){l.path.x+21,l.path.y+1,l.path.w-25,l.path.h-2})) {
        UiRenderer_Text(r,l.path.x+21,l.path.y+4,14,UI_PAPER,path);
        UiRenderer_PopClip(r);
    }

    UiRenderer_Rect(r,l.sidebar,0x0c202eff);
    UiRenderer_Rect(r,(UiRect){l.rows.x-1,l.sidebar.y,1,l.sidebar.h},UI_EDGE);
    UiRenderer_Text(r,c.x+14,l.sidebar.y+5,11,UI_MUTED,"LOCATIONS");
    Place(r,l.root,"/",app->directory == root);
    if (recipes) Place(r,l.recipes,"recepies",app->directory == recipes);
    DrawItems(r,app,l,count);

    UiRenderer_Gradient(r,l.footer,0x102838ff,0x0a1c29ff);
    UiRenderer_Rect(r,(UiRect){l.footer.x,l.footer.y,l.footer.w,1},UI_EDGE);
    char status[48];
    const UiVfsNode *selected = UiVfs_Get(app->fs,app->selected);
    if (app->previewUnavailable) snprintf(status,sizeof(status),"Recipe preview unavailable");
    else if (selected && selected->type == UI_VFS_FILE)
        snprintf(status,sizeof(status),"%d %s  /  %s",count,count==1 ? "item" : "items",Source(selected));
    else snprintf(status,sizeof(status),"%d %s",count,count==1 ? "item" : "items");
    UiRenderer_Text(r,c.x+PAD,l.footer.y+7,12,UI_MUTED,status);
    UiRenderer_Rect(r,(UiRect){c.x+c.w-90,l.footer.y+14,4,4},UI_ACCENT);
    UiRenderer_Text(r,c.x+c.w-80,l.footer.y+7,11,UI_ACCENT,"READ ONLY");
}

void UiFilesView_Input(void *state, UiRect c, const UiEvent *e)
{
    UiFilesApp *app = state;
    Layout l = Geometry(c);
    UiFilesApp_Scroll(app,0,l.visible);
    if (e->type == UI_PRESS) {
        UiVfsId root = UiVfs_Root(app->fs);
        if (UiRect_Contains(l.back,e->x,e->y)) UiFilesApp_Back(app);
        else if (UiRect_Contains(l.up,e->x,e->y)) UiFilesApp_Up(app);
        else if (UiRect_Contains(l.root,e->x,e->y)) UiFilesApp_Go(app,root);
        else if (UiRect_Contains(l.recipes,e->x,e->y))
            UiFilesApp_Go(app,UiVfs_FindChild(app->fs,root,"recepies"));
        else if (UiRect_Contains(l.rows,e->x,e->y)) {
            UiVfsId id = UiFilesApp_Item(app,app->firstRow+(e->y-l.rows.y)/ROW);
            bool activate = e->clicks >= 2 && app->selected == id;
            UiFilesApp_Select(app,id);
            if (activate) UiFilesApp_Activate(app);
        }
    } else if (e->type == UI_WHEEL && UiRect_Contains(l.rows,e->x,e->y)) {
        UiFilesApp_Scroll(app,-e->wheel*3,l.visible);
    } else if (e->type == UI_KEY) {
        switch (e->key) {
        case UI_ENTER: UiFilesApp_Activate(app); break;
        case UI_BACKSPACE: UiFilesApp_Back(app); break;
        case UI_UP: UiFilesApp_MoveSelection(app,-1,l.visible); break;
        case UI_DOWN: UiFilesApp_MoveSelection(app,1,l.visible); break;
        case UI_PAGE_UP: UiFilesApp_Scroll(app,-l.visible,l.visible); break;
        case UI_PAGE_DOWN: UiFilesApp_Scroll(app,l.visible,l.visible); break;
        default: break;
        }
    }
}
