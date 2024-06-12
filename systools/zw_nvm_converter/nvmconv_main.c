/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <user_message.h>
#include "controllerlib_api.h"

nvmlib_interface_t *known_converters[] = {
  &controllerlib_800s_718,
  &controllerlib_800s_717,
  &controllerlib_700s_718,
  &controllerlib_700s_717,
  &controllerlib716,
  &controllerlib715,
  &controllerlib712,
  &controllerlib711,
  &controllerlib68b,
  &controllerlib67b,
  &controllerlib66b,
  &controllerlib68s,
  &controllerlib67s,
  &controllerlib66s,
};

/*****************************************************************************/
static size_t load_file_to_buf(const char* fname, uint8_t **buf)
{
  size_t file_size = 0;
  uint8_t *file_buf = NULL;
  size_t items_read = 0;

  FILE *fp = fopen(fname, "rb");

  if (NULL != fp)
  {
    // Get file size to allocate enough memory
    if (fseek(fp, 0, SEEK_END) == 0)
    {
      file_size = ftell(fp);
    }

    if (file_size > 0)
    {
      rewind(fp);
      file_buf = malloc(file_size);
      if (NULL != file_buf)
      {
        // Here one "item" is a block of "file_size" bytes, i.e. the whole file
        items_read = fread(file_buf, file_size, 1, fp);
      }
    }
    fclose(fp);
  }

  if (items_read > 0)
  {
    *buf = file_buf;
    return file_size;
  }
  return 0;
}

/*****************************************************************************/
static int print_usage(const char* cmd) {
  printf(
      "Usage: %s [-i <format_name>|-e <format_name> ] <src> <dst>\n"
      "\t-e Export mode, produce a JSON file from an NVM file.\n"
      "\t-i Import mode, produce a NVM file from a JSON file.\n\n"
      , cmd);

  printf("Note that for bridge7.16 and onward NVM migration is handled by the Z-Wave\n");
  printf("module automatically so zw_nvm_converter must NOT be used.\n\n");
  printf("supported formats are:\n\n");
  for(int i=0; i < sizeof(known_converters) / sizeof(nvmlib_interface_t*); i++) {
    printf("\t%s : %s\n",known_converters[i]->nvm_desc,known_converters[i]->lib_id  );
  }

  return 1;
}



static nvmlib_interface_t* get_interface_by_name(const char* name) {
  for(int i=0; i < sizeof(known_converters) / sizeof(nvmlib_interface_t*); i++) {
    if(strcmp(name, known_converters[i]->nvm_desc ) == 0) {
      return known_converters[i];
    }
  }
  return 0;
}

/*****************************************************************************/
int main(int argc, char ** argv)
{
  bool import;
  int ch;
  nvmlib_interface_t* nvm_if =0;

  set_message_level(MSG_WARNING);

  while ((ch = getopt(argc, argv, "i:e:")) != -1)
  {
    switch (ch)
    {
    case 'i':
      import = true;
      nvm_if = get_interface_by_name(optarg);
      break;
    case 'e':
      import = false;
      nvm_if = get_interface_by_name(optarg);
      break;
    default:
      return print_usage(argv[0]);
      break;
    }
  }

  if (((argc - optind) != 2) || nvm_if == 0) {
      return print_usage(argv[0]);
  }

  char *in_file  = argv[optind];
  char *out_file = argv[optind + 1];

  if (false == import)
  {
    /* EXPORT: Convert BIN to JSON */

    uint8_t *nvm_buf  = NULL;
    size_t   nvm_size = 0;

    nvm_size = load_file_to_buf(in_file, &nvm_buf);

    if (nvm_size > 0)
    {
      nvm_if->init();
      if (nvm_if->is_nvm_valid(nvm_buf, nvm_size))
      {
        json_object *jo;

        printf("Bin image identified as: %s\n", nvm_if->nvm_desc);
        printf("Using converter: %s\n", nvm_if->lib_id);

        if (nvm_if->nvm_to_json(nvm_buf, nvm_size, &jo))
        {
//          printf("---------------\nGenerated JSON:\n%s\n", json_object_to_json_string_ext(jo, JSON_C_TO_STRING_PRETTY));
          printf("Saving JSON to %s...\n", out_file);

          if (json_object_to_file_ext(out_file, jo, JSON_C_TO_STRING_PRETTY) == -1)
          {
            // json_util_get_last_err() unavailable before json-c 0.13
            // Instead json-c prints directly to stderr 
            // printf("ERROR: %s\n", json_util_get_last_err());
          }
        }
        else
        {
          printf("Failed to convert nvm to json.\n");
        }
      }
      nvm_if->term();
    }
    free(nvm_buf);
  }
  else
  {
    /* IMPORT: Convert JSON to BIN */

    json_object *jo = json_object_from_file(in_file);
    if (jo)
    {
      nvm_if->init();
      if (nvm_if->is_json_valid(jo))
      {
        uint8_t *nvm_buf  = NULL;
        size_t   nvm_size = 0;
        nvm_if->json_to_nvm(jo, &nvm_buf, &nvm_size);
        if (nvm_size > 0)
        {
          printf("got nvm buffer size = %zu\n", nvm_size);
          printf("Saving NVM image to %s...\n", out_file);
          FILE *fp = fopen(out_file, "wb");
          if (fp)
          {
            size_t items_written = fwrite(nvm_buf, nvm_size, 1, fp);
            if (items_written != 1)
            {
              printf("ERROR %d writing to file %s\n", ferror(fp), out_file);
            }
            fclose(fp);
          }
          else
          {
            printf("ERROR creating file %s\n", out_file);
          }

          free(nvm_buf);
        }
        else
        {
          printf("ERROR: Invalid JSON\n");
        }
        
      }
      else
      {
        printf("ERROR: Invalid JSON\n");
      }
      nvm_if->term();
    }
    else
    {
      // json_util_get_last_err() unavailable before json-c 0.13
      // Instead json-c prints directly to stderr 
      // printf("ERROR: %s\n", json_util_get_last_err());
    }
  }

  return 0;
}
