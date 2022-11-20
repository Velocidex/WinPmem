
// Old routines, needs cleaning before being used. :o)

// Get proc address for kernel space. 
// Can support higher irqls.
void * KernelGetProcAddress(void *image_base, char *func_name) 
{
  void *func_address = NULL;

  __try  
  {
    int size = 0;
    IMAGE_DOS_HEADER *dos =(IMAGE_DOS_HEADER *)image_base;
    IMAGE_NT_HEADERS *nt  =(IMAGE_NT_HEADERS *)((uintptr_t)image_base + dos->e_lfanew);

    IMAGE_DATA_DIRECTORY *expdir = (IMAGE_DATA_DIRECTORY *)
      (nt->OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_EXPORT);

    IMAGE_EXPORT_DIRECTORY *exports =(PIMAGE_EXPORT_DIRECTORY)
      ((uintptr_t)image_base + expdir->VirtualAddress);

    uintptr_t addr = (uintptr_t)exports-(uintptr_t)image_base;

    // These are arrays of RVA addresses.
    unsigned int *functions = (unsigned int *)((uintptr_t)image_base +
                                               exports->AddressOfFunctions);

    unsigned int *names = (unsigned int *)((uintptr_t)image_base +
                                           exports->AddressOfNames);

    short *ordinals = (short *)((uintptr_t)image_base +
                                exports->AddressOfNameOrdinals);

    unsigned int max_name  = exports->NumberOfNames;
    unsigned int  max_func  = exports->NumberOfFunctions;

    unsigned int i;

    for (i = 0; i < max_name; i++) {
      unsigned int ord = ordinals[i];
      if(i >= max_name || ord >= max_func) {
        return NULL;
      }

      if (functions[ord] < addr || functions[ord] >= addr + size) {
        if (strcmp((char *)image_base + names[i], func_name)  == 0) {
          func_address = (char *)image_base + functions[ord];
          break;
        }
      }
    }
  }
  __except(EXCEPTION_EXECUTE_HANDLER) 
  {
    func_address = NULL;
  }

  return func_address;
} // end KernelGetProcAddress()



// Search for a section by name.
//
// Returns the mapped virtual memory section or NULL if not found. 
// Can support higher irqls.

IMAGE_SECTION_HEADER* GetSection(IMAGE_DOS_HEADER *image_base, char *name) 
{
  IMAGE_NT_HEADERS *nt  = (IMAGE_NT_HEADERS *)
    ((uintptr_t)image_base + image_base->e_lfanew);
  int i;
  int number_of_sections = nt->FileHeader.NumberOfSections;

  IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)
    ((uintptr_t)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);

  for (i=0; i<number_of_sections; i++) {
    if(!strcmp((char *)sections[i].Name, name))
      return &sections[i];
  };

  return NULL;
};

