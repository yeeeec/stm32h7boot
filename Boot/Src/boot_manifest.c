#include "boot_manifest.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "fatfs.h"
#include "ff.h"
#include "boot_usb.h"
#include "logging.h"

typedef enum
{
  BOOT_JSON_UNDEFINED = 0,
  BOOT_JSON_OBJECT,
  BOOT_JSON_ARRAY,
  BOOT_JSON_STRING,
  BOOT_JSON_PRIMITIVE
} BootJsonType;

typedef struct
{
  BootJsonType type;
  int start;
  int end;
  int size;
  int parent;
} BootJsonToken;

typedef struct
{
  unsigned int pos;
  unsigned int toknext;
  int toksuper;
} BootJsonParser;

static void Boot_Json_Init(BootJsonParser *parser)
{
  parser->pos = 0U;
  parser->toknext = 0U;
  parser->toksuper = -1;
}

static BootJsonToken *Boot_Json_AllocToken(BootJsonParser *parser, BootJsonToken *tokens, unsigned int token_count)
{
  BootJsonToken *token;

  if (parser->toknext >= token_count)
  {
    return NULL;
  }

  token = &tokens[parser->toknext++];
  token->type = BOOT_JSON_UNDEFINED;
  token->start = -1;
  token->end = -1;
  token->size = 0;
  token->parent = -1;

  if (parser->toksuper != -1)
  {
    token->parent = parser->toksuper;
    tokens[parser->toksuper].size++;
  }

  return token;
}

static int Boot_Json_ParseString(BootJsonParser *parser, const char *json, size_t length, BootJsonToken *tokens, unsigned int token_count)
{
  BootJsonToken *token;
  unsigned int start = parser->pos + 1U;

  for (parser->pos = parser->pos + 1U; parser->pos < length; parser->pos++)
  {
    char c = json[parser->pos];
    if (c == '\"')
    {
      token = Boot_Json_AllocToken(parser, tokens, token_count);
      if (token == NULL)
      {
        return -1;
      }

      token->type = BOOT_JSON_STRING;
      token->start = (int)start;
      token->end = (int)parser->pos;
      return 0;
    }

    if (c == '\\')
    {
      parser->pos++;
      if (parser->pos >= length)
      {
        return -1;
      }

      if (json[parser->pos] == 'u')
      {
        for (int i = 0; i < 4; i++)
        {
          parser->pos++;
          if ((parser->pos >= length) || (isxdigit((unsigned char)json[parser->pos]) == 0))
          {
            return -1;
          }
        }
      }
    }
  }

  return -1;
}

static int Boot_Json_ParsePrimitive(BootJsonParser *parser, const char *json, size_t length, BootJsonToken *tokens, unsigned int token_count)
{
  BootJsonToken *token;
  unsigned int start = parser->pos;

  for (; parser->pos < length; parser->pos++)
  {
    char c = json[parser->pos];
    if ((c == '\t') || (c == '\r') || (c == '\n') || (c == ' ') || (c == ',') || (c == ']') || (c == '}'))
    {
      break;
    }

    if ((unsigned char)c < 32U)
    {
      return -1;
    }
  }

  token = Boot_Json_AllocToken(parser, tokens, token_count);
  if (token == NULL)
  {
    return -1;
  }

  token->type = BOOT_JSON_PRIMITIVE;
  token->start = (int)start;
  token->end = (int)parser->pos;
  parser->pos--;
  return 0;
}

static int Boot_Json_Parse(BootJsonParser *parser, const char *json, size_t length, BootJsonToken *tokens, unsigned int token_count)
{
  int result;

  for (; parser->pos < length; parser->pos++)
  {
    char c = json[parser->pos];
    BootJsonToken *token;

    switch (c)
    {
      case '{':
      case '[':
        token = Boot_Json_AllocToken(parser, tokens, token_count);
        if (token == NULL)
        {
          return -1;
        }
        token->type = (c == '{') ? BOOT_JSON_OBJECT : BOOT_JSON_ARRAY;
        token->start = (int)parser->pos;
        parser->toksuper = (int)(parser->toknext - 1U);
        break;

      case '}':
      case ']':
      {
        BootJsonType type = (c == '}') ? BOOT_JSON_OBJECT : BOOT_JSON_ARRAY;
        int i;

        for (i = (int)parser->toknext - 1; i >= 0; i--)
        {
          if ((tokens[i].start != -1) && (tokens[i].end == -1))
          {
            if (tokens[i].type != type)
            {
              return -1;
            }
            tokens[i].end = (int)parser->pos + 1;
            parser->toksuper = tokens[i].parent;
            break;
          }
        }

        if (i == -1)
        {
          return -1;
        }
        break;
      }

      case '\"':
        result = Boot_Json_ParseString(parser, json, length, tokens, token_count);
        if (result != 0)
        {
          return -1;
        }
        break;

      case '\t':
      case '\r':
      case '\n':
      case ' ':
        break;

      case ':':
        parser->toksuper = (int)(parser->toknext - 1U);
        break;

      case ',':
        if ((parser->toksuper != -1) &&
            (tokens[parser->toksuper].type != BOOT_JSON_ARRAY) &&
            (tokens[parser->toksuper].type != BOOT_JSON_OBJECT))
        {
          parser->toksuper = tokens[parser->toksuper].parent;
        }
        break;

      default:
        result = Boot_Json_ParsePrimitive(parser, json, length, tokens, token_count);
        if (result != 0)
        {
          return -1;
        }
        break;
    }
  }

  for (unsigned int i = 0; i < parser->toknext; i++)
  {
    if ((tokens[i].start != -1) && (tokens[i].end == -1))
    {
      return -1;
    }
  }

  return (int)parser->toknext;
}

static int Boot_Json_TokenEq(const char *json, const BootJsonToken *token, const char *value)
{
  size_t value_length;
  size_t token_length;

  if ((json == NULL) || (token == NULL) || (value == NULL))
  {
    return 0;
  }
  value_length = strlen(value);
  token_length = (size_t)(token->end - token->start);

  return ((token_length == value_length) && (memcmp(&json[token->start], value, value_length) == 0)) ? 1 : 0;
}

static int Boot_Json_TokenCopyString(const char *json, const BootJsonToken *token, char *buffer, size_t buffer_length)
{
  size_t token_length;

  if ((json == NULL) || (token == NULL) || (buffer == NULL) || (buffer_length == 0U) || (token->type != BOOT_JSON_STRING))
  {
    return -1;
  }

  token_length = (size_t)(token->end - token->start);
  if (token_length >= buffer_length)
  {
    return -1;
  }

  memcpy(buffer, &json[token->start], token_length);
  buffer[token_length] = '\0';
  return 0;
}

static int Boot_Json_TokenToU32(const char *json, const BootJsonToken *token, uint32_t *value)
{
  char number_buffer[32];
  char *end_ptr;
  unsigned long converted;
  size_t token_length;

  if ((json == NULL) || (token == NULL) || (value == NULL))
  {
    return -1;
  }

  if ((token->type != BOOT_JSON_PRIMITIVE) && (token->type != BOOT_JSON_STRING))
  {
    return -1;
  }

  token_length = (size_t)(token->end - token->start);
  if ((token_length == 0U) || (token_length >= sizeof(number_buffer)))
  {
    return -1;
  }

  memcpy(number_buffer, &json[token->start], token_length);
  number_buffer[token_length] = '\0';

  converted = strtoul(number_buffer, &end_ptr, 0);
  if ((*end_ptr != '\0') || (converted > 0xFFFFFFFFUL))
  {
    return -1;
  }

  *value = (uint32_t)converted;
  return 0;
}

static int Boot_Json_TokenToBool(const char *json, const BootJsonToken *token, uint8_t *value)
{
  uint32_t numeric;

  if ((json == NULL) || (token == NULL) || (value == NULL))
  {
    return -1;
  }

  if (Boot_Json_TokenEq(json, token, "true"))
  {
    *value = 1U;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "false"))
  {
    *value = 0U;
    return 0;
  }

  if (Boot_Json_TokenToU32(json, token, &numeric) == 0)
  {
    *value = (numeric != 0U) ? 1U : 0U;
    return 0;
  }

  return -1;
}

static int Boot_Json_TokenNext(const BootJsonToken *tokens, int token_count, int token_index)
{
  int i;
  int end;

  if ((tokens == NULL) || (token_index < 0) || (token_index >= token_count))
  {
    return token_count;
  }

  end = tokens[token_index].end;
  i = token_index + 1;
  while ((i < token_count) && (tokens[i].start < end))
  {
    i++;
  }

  return i;
}

static int Boot_Json_FindObjectValue(const char *json,
                                     const BootJsonToken *tokens,
                                     int token_count,
                                     int object_index,
                                     const char *key)
{
  int cursor;

  (void)json;
  if ((tokens == NULL) || (object_index < 0) || (object_index >= token_count) ||
      (tokens[object_index].type != BOOT_JSON_OBJECT))
  {
    return -1;
  }

  cursor = object_index + 1;
  for (int i = 0; i < tokens[object_index].size; i++)
  {
    int key_index = cursor;
    int value_index = cursor + 1;

    if (value_index >= token_count)
    {
      return -1;
    }

    if ((tokens[key_index].type == BOOT_JSON_STRING) && Boot_Json_TokenEq(json, &tokens[key_index], key))
    {
      return value_index;
    }

    cursor = Boot_Json_TokenNext(tokens, token_count, value_index);
  }

  return -1;
}

static int Boot_Manifest_ParseTargetSlot(const char *json, const BootJsonToken *token, BootManifestTargetSlot *target_slot)
{
  if ((json == NULL) || (token == NULL) || (target_slot == NULL) || (token->type != BOOT_JSON_STRING))
  {
    return -1;
  }

  if (Boot_Json_TokenEq(json, token, "app1"))
  {
    *target_slot = BOOT_MANIFEST_SLOT_APP1;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "app2"))
  {
    *target_slot = BOOT_MANIFEST_SLOT_APP2;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "inactive"))
  {
    *target_slot = BOOT_MANIFEST_SLOT_INACTIVE;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "none"))
  {
    *target_slot = BOOT_MANIFEST_SLOT_NONE;
    return 0;
  }

  return -1;
}

static int Boot_Manifest_ParseConfigRegion(const char *json, const BootJsonToken *token, BootManifestConfigRegion *region)
{
  if ((json == NULL) || (token == NULL) || (region == NULL) || (token->type != BOOT_JSON_STRING))
  {
    return -1;
  }

  if (Boot_Json_TokenEq(json, token, "sys_config"))
  {
    *region = BOOT_MANIFEST_CONFIG_REGION_SYS_CONFIG;
    return 0;
  }

  return -1;
}

static int Boot_Manifest_ParseBootFlag(const char *json, const BootJsonToken *token, BootManifestBootFlag *flag)
{
  if ((json == NULL) || (token == NULL) || (flag == NULL) || (token->type != BOOT_JSON_STRING))
  {
    return -1;
  }

  if (Boot_Json_TokenEq(json, token, "request_verify"))
  {
    *flag = BOOT_MANIFEST_BOOT_FLAG_REQUEST_VERIFY;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "request_upgrade"))
  {
    *flag = BOOT_MANIFEST_BOOT_FLAG_REQUEST_UPGRADE;
    return 0;
  }

  if (Boot_Json_TokenEq(json, token, "force_upgrade"))
  {
    *flag = BOOT_MANIFEST_BOOT_FLAG_FORCE_UPGRADE;
    return 0;
  }

  return -1;
}

static BootError Boot_Manifest_ParsePolicy(const char *json,
                                           const BootJsonToken *tokens,
                                           int token_count,
                                           int policy_token_index,
                                           BootManifestPolicy *policy)
{
  int token_index;
  uint32_t max_boot_attempts;

  if ((json == NULL) || (tokens == NULL) || (policy == NULL) || (policy_token_index < 0))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (tokens[policy_token_index].type != BOOT_JSON_OBJECT)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, policy_token_index, "allow_downgrade");
  if ((token_index >= 0) && (Boot_Json_TokenToBool(json, &tokens[token_index], &policy->allow_downgrade) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, policy_token_index, "require_signature");
  if ((token_index >= 0) && (Boot_Json_TokenToBool(json, &tokens[token_index], &policy->require_signature) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, policy_token_index, "max_boot_attempts");
  if (token_index >= 0)
  {
    if (Boot_Json_TokenToU32(json, &tokens[token_index], &max_boot_attempts) != 0)
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }
    if (max_boot_attempts > 0xFFU)
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }
    policy->max_boot_attempts = (uint8_t)max_boot_attempts;
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Manifest_ParseSignature(const char *json,
                                              const BootJsonToken *tokens,
                                              int token_count,
                                              int signature_token_index,
                                              BootManifestSignature *signature)
{
  int token_index;

  if ((json == NULL) || (tokens == NULL) || (signature == NULL) || (signature_token_index < 0))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (tokens[signature_token_index].type != BOOT_JSON_OBJECT)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, signature_token_index, "file");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], signature->file, sizeof(signature->file)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, signature_token_index, "algo");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], signature->algo, sizeof(signature->algo)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Manifest_ParsePreserveArray(const char *json,
                                                  const BootJsonToken *tokens,
                                                  int token_count,
                                                  int array_token_index,
                                                  BootManifestWriteConfig *write_config)
{
  int cursor;

  if ((json == NULL) || (tokens == NULL) || (write_config == NULL) || (array_token_index < 0))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (tokens[array_token_index].type != BOOT_JSON_ARRAY)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  if ((uint32_t)tokens[array_token_index].size > BOOT_MANIFEST_MAX_PRESERVE_ITEMS)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  write_config->preserve_count = (uint32_t)tokens[array_token_index].size;
  cursor = array_token_index + 1;

  for (uint32_t i = 0U; i < write_config->preserve_count; i++)
  {
    if ((cursor >= token_count) ||
        (Boot_Json_TokenCopyString(json, &tokens[cursor], write_config->preserve[i], sizeof(write_config->preserve[i])) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }
    cursor = Boot_Json_TokenNext(tokens, token_count, cursor);
  }

  return BOOT_ERR_NONE;
}

static BootError Boot_Manifest_ParseOperation(const char *json,
                                              const BootJsonToken *tokens,
                                              int token_count,
                                              int operation_token_index,
                                              BootManifestOperation *operation)
{
  int token_index;
  uint32_t number_value;
  char type_buffer[BOOT_MANIFEST_TEXT_LENGTH];

  if ((json == NULL) || (tokens == NULL) || (operation == NULL) ||
      (operation_token_index < 0) || (tokens[operation_token_index].type != BOOT_JSON_OBJECT))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  memset(operation, 0, sizeof(*operation));
  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "type");
  if ((token_index < 0) ||
      (Boot_Json_TokenCopyString(json, &tokens[token_index], type_buffer, sizeof(type_buffer)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  if (strcmp(type_buffer, "write_app") == 0)
  {
    operation->type = BOOT_OP_WRITE_APP;
    operation->payload.write_app.target_slot = BOOT_MANIFEST_SLOT_INACTIVE;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "file");
    if ((token_index < 0) ||
        (Boot_Json_TokenCopyString(json, &tokens[token_index], operation->payload.write_app.file,
                                   sizeof(operation->payload.write_app.file)) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "target_slot");
    if ((token_index >= 0) &&
        (Boot_Manifest_ParseTargetSlot(json, &tokens[token_index], &operation->payload.write_app.target_slot) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "offset");
    if ((token_index >= 0) &&
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_app.offset) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "size");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_app.size) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "crc32");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_app.crc32) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    return BOOT_ERR_NONE;
  }

  if (strcmp(type_buffer, "write_config") == 0)
  {
    operation->type = BOOT_OP_WRITE_CONFIG;
    operation->payload.write_config.target_region = BOOT_MANIFEST_CONFIG_REGION_SYS_CONFIG;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "file");
    if ((token_index < 0) ||
        (Boot_Json_TokenCopyString(json, &tokens[token_index], operation->payload.write_config.file,
                                   sizeof(operation->payload.write_config.file)) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "target_region");
    if ((token_index >= 0) &&
        (Boot_Manifest_ParseConfigRegion(json, &tokens[token_index], &operation->payload.write_config.target_region) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "size");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_config.size) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "crc32");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_config.crc32) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "preserve");
    if (token_index >= 0)
    {
      BootError error = Boot_Manifest_ParsePreserveArray(json, tokens, token_count, token_index, &operation->payload.write_config);
      if (error != BOOT_ERR_NONE)
      {
        return error;
      }
    }

    return BOOT_ERR_NONE;
  }

  if (strcmp(type_buffer, "write_extflash") == 0)
  {
    operation->type = BOOT_OP_WRITE_EXTFLASH;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "file");
    if ((token_index < 0) ||
        (Boot_Json_TokenCopyString(json, &tokens[token_index], operation->payload.write_extflash.file,
                                   sizeof(operation->payload.write_extflash.file)) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "offset");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_extflash.offset) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "size");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_extflash.size) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "crc32");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.write_extflash.crc32) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    return BOOT_ERR_NONE;
  }

  if (strcmp(type_buffer, "erase_extflash") == 0)
  {
    operation->type = BOOT_OP_ERASE_EXTFLASH;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "offset");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.erase_extflash.offset) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "size");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.erase_extflash.size) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    return BOOT_ERR_NONE;
  }

  if (strcmp(type_buffer, "dump_extflash") == 0)
  {
    operation->type = BOOT_OP_DUMP_EXTFLASH;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "offset");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.dump_extflash.offset) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "size");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &operation->payload.dump_extflash.size) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "output");
    if ((token_index < 0) ||
        (Boot_Json_TokenCopyString(json, &tokens[token_index], operation->payload.dump_extflash.output,
                                   sizeof(operation->payload.dump_extflash.output)) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    return BOOT_ERR_NONE;
  }

  if (strcmp(type_buffer, "set_boot_flag") == 0)
  {
    operation->type = BOOT_OP_SET_BOOT_FLAG;

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "flag");
    if ((token_index < 0) ||
        (Boot_Manifest_ParseBootFlag(json, &tokens[token_index], &operation->payload.set_boot_flag.flag) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    token_index = Boot_Json_FindObjectValue(json, tokens, token_count, operation_token_index, "value");
    if ((token_index < 0) ||
        (Boot_Json_TokenToU32(json, &tokens[token_index], &number_value) != 0))
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }
    operation->payload.set_boot_flag.value = number_value;
    return BOOT_ERR_NONE;
  }

  return BOOT_ERR_MANIFEST_PARSE;
}

static BootError Boot_Manifest_ParseOperations(const char *json,
                                               const BootJsonToken *tokens,
                                               int token_count,
                                               int operations_token_index,
                                               BootManifest *manifest)
{
  int cursor;

  if ((json == NULL) || (tokens == NULL) || (manifest == NULL) || (operations_token_index < 0))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (tokens[operations_token_index].type != BOOT_JSON_ARRAY)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  if (tokens[operations_token_index].size <= 0)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  if ((uint32_t)tokens[operations_token_index].size > BOOT_MANIFEST_MAX_OPERATIONS)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  manifest->operation_count = (uint32_t)tokens[operations_token_index].size;
  cursor = operations_token_index + 1;

  for (uint32_t i = 0U; i < manifest->operation_count; i++)
  {
    BootError error;

    if (cursor >= token_count)
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }

    error = Boot_Manifest_ParseOperation(json, tokens, token_count, cursor, &manifest->operations[i]);
    if (error != BOOT_ERR_NONE)
    {
      return error;
    }
    cursor = Boot_Json_TokenNext(tokens, token_count, cursor);
  }

  return BOOT_ERR_NONE;
}

static void Boot_Manifest_ApplyDefaults(BootManifest *manifest)
{
  strncpy(manifest->vendor, BOOT_VENDOR_NAME, sizeof(manifest->vendor) - 1U);
  strncpy(manifest->product, BOOT_PRODUCT_NAME, sizeof(manifest->product) - 1U);
  strncpy(manifest->board, BOOT_BOARD_NAME, sizeof(manifest->board) - 1U);
  manifest->policy.allow_downgrade = 0U;
  manifest->policy.require_signature = 0U;
  manifest->policy.max_boot_attempts = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
  strncpy(manifest->signature.file, "signature.sig", sizeof(manifest->signature.file) - 1U);
  strncpy(manifest->signature.algo, "sha256+rsa2048", sizeof(manifest->signature.algo) - 1U);
}

void Boot_Manifest_Reset(BootManifest *manifest)
{
  if (manifest == NULL)
  {
    return;
  }

  memset(manifest, 0, sizeof(*manifest));
}

static BootError Boot_Manifest_ParseRoot(const char *json,
                                         const BootJsonToken *tokens,
                                         int token_count,
                                         BootManifest *manifest)
{
  int token_index;
  uint32_t number_value;
  BootError error;

  if ((json == NULL) || (tokens == NULL) || (token_count <= 0) || (manifest == NULL))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (tokens[0].type != BOOT_JSON_OBJECT)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "format_version");
  if ((token_index < 0) || (Boot_Json_TokenToU32(json, &tokens[token_index], &manifest->format_version) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "vendor");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->vendor, sizeof(manifest->vendor)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "product");
  if ((token_index < 0) ||
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->product, sizeof(manifest->product)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "board");
  if ((token_index < 0) ||
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->board, sizeof(manifest->board)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "bundle_version");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->bundle_version, sizeof(manifest->bundle_version)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "bundle_id");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->bundle_id, sizeof(manifest->bundle_id)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "serial_limit");
  if ((token_index >= 0) &&
      (Boot_Json_TokenCopyString(json, &tokens[token_index], manifest->serial_limit, sizeof(manifest->serial_limit)) != 0))
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "policy");
  if (token_index >= 0)
  {
    error = Boot_Manifest_ParsePolicy(json, tokens, token_count, token_index, &manifest->policy);
    if (error != BOOT_ERR_NONE)
    {
      return error;
    }
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "operations");
  if (token_index < 0)
  {
    return BOOT_ERR_MANIFEST_PARSE;
  }

  error = Boot_Manifest_ParseOperations(json, tokens, token_count, token_index, manifest);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  token_index = Boot_Json_FindObjectValue(json, tokens, token_count, 0, "signature");
  if (token_index >= 0)
  {
    error = Boot_Manifest_ParseSignature(json, tokens, token_count, token_index, &manifest->signature);
    if (error != BOOT_ERR_NONE)
    {
      return error;
    }
  }

  if (strcmp(manifest->product, BOOT_PRODUCT_NAME) != 0)
  {
    return BOOT_ERR_PRODUCT_MISMATCH;
  }

  if (strcmp(manifest->board, BOOT_BOARD_NAME) != 0)
  {
    return BOOT_ERR_BOARD_MISMATCH;
  }

  if (manifest->policy.max_boot_attempts == 0U)
  {
    manifest->policy.max_boot_attempts = BOOT_PENDING_SLOT_MAX_ATTEMPTS;
  }
  else
  {
    number_value = manifest->policy.max_boot_attempts;
    if (number_value > 0xFFU)
    {
      return BOOT_ERR_MANIFEST_PARSE;
    }
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Manifest_Load(BootManifest *manifest)
{
  FIL manifest_file;
  UINT bytes_read;
  FRESULT fatfs_result;
  BootError error;
  char path_buffer[64];
  char json_buffer[BOOT_MANIFEST_FILE_MAX_SIZE + 1U];
  BootJsonParser parser;
  BootJsonToken tokens[BOOT_MANIFEST_TOKEN_MAX];
  int token_count;
  uint32_t file_size;

  if (manifest == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  Boot_Manifest_Reset(manifest);
  Boot_Manifest_ApplyDefaults(manifest);

  error = Boot_Usb_BuildPath(BOOT_MANIFEST_PATH, path_buffer, sizeof(path_buffer));
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  fatfs_result = f_open(&manifest_file, path_buffer, FA_READ);
  if (fatfs_result != FR_OK)
  {
    logging_write(LOG_LVL_ERROR, "MANIFEST", "open failed: %s", path_buffer);
    return BOOT_ERR_MANIFEST_NOT_FOUND;
  }

  file_size = (uint32_t)f_size(&manifest_file);
  if ((file_size == 0U) || (file_size > BOOT_MANIFEST_FILE_MAX_SIZE))
  {
    (void)f_close(&manifest_file);
    logging_write(LOG_LVL_ERROR, "MANIFEST", "invalid size: %lu", (unsigned long)file_size);
    return BOOT_ERR_MANIFEST_PARSE;
  }

  bytes_read = 0U;
  fatfs_result = f_read(&manifest_file, json_buffer, file_size, &bytes_read);
  (void)f_close(&manifest_file);
  if ((fatfs_result != FR_OK) || (bytes_read != file_size))
  {
    logging_write(LOG_LVL_ERROR, "MANIFEST", "read failed");
    return BOOT_ERR_MANIFEST_PARSE;
  }
  json_buffer[file_size] = '\0';

  Boot_Json_Init(&parser);
  token_count = Boot_Json_Parse(&parser, json_buffer, file_size, tokens, BOOT_MANIFEST_TOKEN_MAX);
  if (token_count <= 0)
  {
    logging_write(LOG_LVL_ERROR, "MANIFEST", "json tokenize failed");
    return BOOT_ERR_MANIFEST_PARSE;
  }

  error = Boot_Manifest_ParseRoot(json_buffer, tokens, token_count, manifest);
  if (error != BOOT_ERR_NONE)
  {
    logging_write(LOG_LVL_ERROR, "MANIFEST", "parse root failed: %lu", (unsigned long)error);
    return error;
  }

  logging_write(LOG_LVL_INFO,
                        "MANIFEST",
                        "parsed ok, ops=%lu, bundle=%s",
                        (unsigned long)manifest->operation_count,
                        manifest->bundle_id);
  return BOOT_ERR_NONE;
}
