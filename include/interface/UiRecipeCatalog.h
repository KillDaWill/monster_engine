#ifndef UI_RECIPE_CATALOG_H
#define UI_RECIPE_CATALOG_H

#include "UiVfs.h"

typedef struct {
    bool manifestAvailable;
    unsigned rejected, duplicates, external, builtin;
} UiRecipeCatalogReport;

/* directory es una ruta del proyecto elegida por la aplicación, nunca por el terminal.
 * Agrega entradas a un directorio virtual vacío. NULL conserva el catálogo incorporado. */
UiRecipeCatalogReport UiRecipeCatalog_Load(UiVfs *, UiVfsId, const char *directory);
/* Busca el proyecto junto a demos/interface_demo, con la fuente del demo morph como marcador. */
UiRecipeCatalogReport UiRecipeCatalog_LoadProject(UiVfs *, UiVfsId, const char *executableDirectory);
/* Lee sólo la entrada externa que la aplicación resolvió en el árbol del catálogo.
 * El llamador libera el JSON y comprueba su ID al validarlo con el motor. */
char *UiRecipeCatalog_ReadProjectRecipe(const char *executableDirectory, const UiVfsNode *);
#endif
