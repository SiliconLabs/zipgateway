<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
xmlns:fo="http://www.w3.org/1999/XSL/Format" >
<xsl:output method="text" omit-xml-declaration="yes" indent="no"/>
<xsl:key name="ClassName" match="zw_classes/cmd_class" use="@name" />
<xsl:key name="ClassKey" match="zw_classes/cmd_class" use="@key" />

<!-- Set next variable to 'true' to include DBG_PRINTF commands -->
<xsl:variable name="debug" select="'false'" />

<!--
	XSLT variable "param_key_array_size" is used to dimension c-arrays holding
	offsets of command class parameters. For thos arrays to be sufficiently large
	"param_key_array_size" must be set to at least one higher than the max value
	of any param key attribute. The value must be written in hex with upper case
	character like '0x1A'.
	As a safe-guard, the XSLT transformation will be aborted if a key attribute
	with a value equal to param_key_array_size is encountered.
-->
<xsl:variable name="param_key_array_size" select="'0x19'" />

<xsl:template name="const_validator">
	if(passed >=cmdLen) return PARSE_FAIL;
	switch(cmd[passed]) {
	<xsl:for-each select="./const">
		case <xsl:value-of select="./@flagmask"/>: /* <xsl:value-of select="./@flagname"/> */</xsl:for-each>
			break;
		default:
			return UNKNOWN_PARAMETER;
	}
	passed++;
</xsl:template>

<xsl:template name="enum_validator">
	if(passed >=cmdLen) return PARSE_FAIL;
	switch(cmd[passed]) {
	<xsl:for-each select="./enum">
		case <xsl:value-of select="./@key"/>: /* <xsl:value-of select="./@name"/> */ </xsl:for-each>
			break;
		default:
			return UNKNOWN_PARAMETER;
	}
	passed++;
</xsl:template>

<xsl:template name="array_validator">
	<xsl:choose>
	<xsl:when test="./arrayattrib/@len = '255'">
		<!--
			This case (which aparently indicates variable size arrays) is not
			use in the current XML file. No need to maintain code to handle it.
		-->
	</xsl:when>
	<xsl:otherwise>
	passed +=<xsl:value-of select="./arrayattrib/@len"/>;
	</xsl:otherwise>
	</xsl:choose>	
</xsl:template>


<xsl:template name="variant_validator">
	<xsl:choose>
	<!--
		paramoffs is checked here with string compare. Will fail if the XML file
		is change to using '0xFF' (like paramOffs for variant groups)
	-->
	<xsl:when test="./variant/@paramoffs = '255'">
	{
		/* @paramoffs == '255' for this variant, i.e. the length is
		 * determined by the command length, so we skip any further validation
		 */
		uint16_t variant_start = passed; /* Not used for anything */
	}
	</xsl:when>
	<xsl:otherwise>

	<xsl:for-each select="./variant">
	<xsl:if test="../@optionaloffs">
	if (check_optional_param_flag(cmd, cmdLen, paramkey_index, parent_paramkey_index,
																<xsl:value-of select="../@optionaloffs"/>,
																<xsl:value-of select="../@optionalmask"/> + 0
																))
	</xsl:if>
	{
		validator_result_t res;
		res = validate_variant(cmd, cmdLen, paramkey_index, parent_paramkey_index, &amp;passed,
														<xsl:value-of select="./@paramoffs"/>,
														<xsl:value-of select="./@sizemask"/>,
														<xsl:value-of select="./@sizeoffs"/> + 0,
														<xsl:value-of select="./@sizechange"/> + 0
													);
		if (res != PARSE_OK)
		{
			return res;
		}
	}
	</xsl:for-each>
	</xsl:otherwise>
	</xsl:choose>
</xsl:template>


<xsl:template name="byte_validator">
	<xsl:if test="./@optionaloffs">
	if (check_optional_param_flag(cmd, cmdLen, paramkey_index, parent_paramkey_index,
																<xsl:value-of select="./@optionaloffs"/>,
																<xsl:value-of select="./@optionalmask"/> + 0
																))
	</xsl:if>
	{
	<!--
		The BYTE validation using bitflags has been disabled here since is seems
		to be defined inconsistently in the XML file. Some places it is a mask
		while in other places it's a list of actual values.
		The (commented out) code here assumes bitflags are masks and combines them
		into a single mask and then checks if any bits outside the valid mask are
		set.

	<xsl:if test="./bitflag">
		if (passed >= cmdLen) return PARSE_FAIL;
		uint8_t valid_bits_mask = 0 <xsl:for-each select="./bitflag">
			| <xsl:value-of select="./@flagmask"/> /* <xsl:value-of select="./@flagname"/> */</xsl:for-each>
			;
		if (cmd[passed] &amp; ~valid_bits_mask)
		{
			return UNKNOWN_PARAMETER;
		}
	</xsl:if>

	-->
		passed++;
	}
</xsl:template>


<xsl:template name="multi_array_validator">
	{
		if (passed >= cmdLen) return PARSE_FAIL;

		uint16_t val1_param_index = get_index_of_param(paramkey_index, parent_paramkey_index, <xsl:value-of select="./multi_array/paramdescloc/@param"/>);
		if (val1_param_index >= cmdLen)
		{
			return PARSE_FAIL;
		}
		uint8_t val1 = cmd[val1_param_index]; // MSB
		uint8_t val2 = cmd[passed];           // LSB
		<xsl:if test="$debug='true'">DBG_PRINTF("MULTI_ARRAY MSB:0x%02x LSB:0x%02x\n", val1, val2);</xsl:if>
		<xsl:variable name="select_key" select="concat('0x0', ./multi_array/paramdescloc/@param)"/>
		<xsl:variable name="select_node" select="../param[@key=$select_key]"/>
		/* Selecting on <xsl:value-of select="$select_node/@name"/> */
		switch (val1) 
		{
			<xsl:for-each select="./multi_array[bitflag]">
			<xsl:variable name="val1" select="./bitflag[1]/@key"/>
			case <xsl:value-of select="$val1"/>: /* <xsl:value-of select="../../param[@key=$select_key]/*[@flagmask=$val1]/@flagname"/> */

				switch (val2)
				{	
					<xsl:for-each select="./bitflag">
					case <xsl:value-of select="@flagmask"/>: /* <xsl:value-of select="@flagname"/>*/</xsl:for-each>
						break;
					default:
						return UNKNOWN_PARAMETER;
				}
				break;
			</xsl:for-each>
		}
	}
	passed++;
</xsl:template>


<xsl:template name="variant_group_validator">
	<xsl:if test="./@optionaloffs">
	if (check_optional_param_flag(cmd, cmdLen, paramkey_index, parent_paramkey_index,
																<xsl:value-of select="./@optionaloffs"/>,
																<xsl:value-of select="./@optionalmask"/> + 0
																))
	</xsl:if>
	{
	<xsl:choose>
	<!--
		paramOffs is checked here with string compare. Will fail if the XML file
		is change to using '255' (like paramoffs used for variants)
	-->
	<xsl:when test="./@paramOffs = '0xFF'">
	{
		/* @paramOffs == '0xFF' for this variant group, i.e. the length is
		 * determined by the command length, so we skip any further validation
		 */
		uint16_t variant_group_start = passed; /* Not used for anything */
	}
	</xsl:when>
	<xsl:otherwise>
		<!--
			We're leaving the limit check below for the C pre-processor since the numbers
			could be hex and decimal. That's not trivial to process with XSLT 1.0.
		-->
		#if (<xsl:value-of select="./@paramOffs"/> &lt; 0) || (<xsl:value-of select="./@paramOffs"/> > MAX_PARAM_KEY_VALUE) || (<xsl:value-of select="./@paramOffs"/> > <xsl:value-of select="./@key"/>)
			#error "KEY_BOUNDS_ERROR (paramOffs value = <xsl:value-of select="./@paramOffs"/>). See comment at top of file."
			count of groups. The index of that parameter is stored in paramkey_index
		#endif

		<!--
			paramOffs is the value of @key attribute of parameter containing the
			number of variant groups.
		-->
		uint16_t vg_count_index = get_index_of_param(paramkey_index, parent_paramkey_index, <xsl:value-of select="./@paramOffs"/>);
		if (vg_count_index >= cmdLen)
		{
			return PARSE_FAIL;
		}
		uint8_t vg_count = (cmd[vg_count_index] &amp; <xsl:value-of select="./@sizemask"/>) >> <xsl:value-of select="./@sizeoffs"/>;

		<xsl:if test="$debug='true'">DBG_PRINTF("vg_count = %d\n", vg_count);</xsl:if>
		
		/* Parsing parameters in variant group */
		parent_paramkey_index = paramkey_index;
		while (vg_count > 0)
		{
			if (passed >= cmdLen) return PARSE_FAIL;

			{ /* ### BEGIN variant group parameters */
				uint16_t paramkey_index[MAX_PARAM_KEYS] = {0}; // This array is local to variant group
		<xsl:for-each select="./param">
		<xsl:call-template name="element_validator"/>
		</xsl:for-each>
			} /* ### END variant group parameters */
			vg_count--;
		}
		parent_paramkey_index = NULL;
	</xsl:otherwise>
	</xsl:choose>
	}
</xsl:template>


<xsl:template name="parameter_validator">
	<xsl:choose>
	<xsl:when test="@type = 'BYTE'"><xsl:call-template name="byte_validator"/></xsl:when>
	<xsl:when test="@type = 'WORD'">passed+=2;</xsl:when>
	<xsl:when test="@type = 'DWORD'">passed+=4;</xsl:when>
	<xsl:when test="@type = 'BIT_24'">passed+=3;</xsl:when>
	<xsl:when test="@type = 'ARRAY'"><xsl:call-template name="array_validator"/> </xsl:when>
	<xsl:when test="@type = 'BITMASK'">passed++;</xsl:when>
	<xsl:when test="@type = 'STRUCT_BYTE'">passed++;</xsl:when>
	<xsl:when test="@type = 'ENUM'"><xsl:call-template name="enum_validator"/> </xsl:when>
	<xsl:when test="@type = 'ENUM_ARRAY'"><xsl:call-template name="enum_validator"/> </xsl:when>
	<xsl:when test="@type = 'MULTI_ARRAY'"><xsl:call-template name="multi_array_validator"/> </xsl:when>
	<xsl:when test="@type = 'CONST'"> <xsl:call-template name="const_validator"/> </xsl:when>
	<xsl:when test="@type = 'VARIANT'"><xsl:call-template name="variant_validator"/></xsl:when>
	</xsl:choose>
</xsl:template>


<xsl:template name="element_validator">
	<xsl:choose>
		<xsl:when test="name(.) = 'param'">
	/* Parsing <xsl:value-of select="./@type"/> --- <xsl:value-of select="./@name"/> */
		</xsl:when>
		<xsl:when test="name(.) = 'variant_group'">
	/* Parsing variant group <xsl:value-of select="./@name"/> */
		</xsl:when>
		<xsl:otherwise>
			<xsl:message terminate="yes">
	ERROR: Unknown cmd element: <xsl:value-of select="name(.)"/>
			</xsl:message>
		</xsl:otherwise>
	</xsl:choose>

	<!--
		We're leaving the limit check below for the C pre-processor since the numbers
		are (mostly) hex values. That's not trivial to process with XSLT 1.0.
	-->
	#if (<xsl:value-of select="./@key"/> &lt; 0) || (<xsl:value-of select="./@key"/> > MAX_PARAM_KEY_VALUE)
		#error "KEY_BOUNDS_ERROR (key value = <xsl:value-of select="./@key"/>). See comment at top of file."
	#endif

	save_index_of_param(paramkey_index, <xsl:value-of select="./@key"/>, passed);

	<xsl:choose>
		<xsl:when test="name(.) = 'param'">
			<xsl:call-template name="parameter_validator"/>
		</xsl:when>
		<xsl:when test="name(.) = 'variant_group'">
			<xsl:call-template name="variant_group_validator"/>
		</xsl:when>
	</xsl:choose>
</xsl:template>


<xsl:template name="cmd_validator">
static validator_result_t <xsl:value-of select="../@name"/>_v<xsl:value-of select="../@version"/>_<xsl:value-of select="./@name"/>_validator(uint8_t* cmd, int cmdLen ) {
	uint16_t paramkey_index[MAX_PARAM_KEYS] = {0};
	uint16_t *parent_paramkey_index = NULL;
	<xsl:if test="$debug='true'">DBG_PRINTF("-----> %s\n", __func__);</xsl:if>

	uint16_t passed = 0;

	<xsl:for-each select="./*">
		<xsl:call-template name="element_validator"/>
	</xsl:for-each>

	if (passed > cmdLen) return PARSE_FAIL;

	return PARSE_OK;
}
</xsl:template>


<xsl:template match="/">

<xsl:variable name="cmd_classes" select="zw_classes/cmd_class[generate-id() = generate-id(key('ClassKey', @key)[last()])]"/>
/**************************************************************************
 * AUTO-GENERATED FILE. DO NOT MODIFY!
 *
 * Normally generated from ZWave_custom_cmd_classes.xml with cvalidator.xsl
 **************************************************************************/


/**************************************************************************
 * Description of KEY_BOUNDS_ERROR
 *
 * If the pre-processor fails due to this error it means that in the XML
 * file a parameter or variant group exist with a 'key' or 'paramOffs'
 * attribute value that exceeds the currently configured limit.
 *
 * - For 'key' attributes this could happen if one or more parameters are
 *   added to a (large) command class.
 *   TO FIX IT: Increase the value of XSLT variable $param_key_array_size
 *              in cvalidator.xsl
 *
 * - For 'paramOffs' attributes it could indicate an error in the XML file.
 *   TO FIX IT: Check the XML file and confirm that the paramOffs attribute
 *              references an existing key value.
 **************************************************************************/

#include "ZW_command_validator.h"
#include "ZW_typedefs.h"
<xsl:if test="$debug='true'">#include "ZIP_Router_logging.h"</xsl:if>

#define MAX_PARAM_KEYS      <xsl:value-of select="$param_key_array_size"/>
#define MAX_PARAM_KEY_VALUE (MAX_PARAM_KEYS - 1)

#define PARAMETER_KEY_BELONGS_TO_PARENT_BITFLAG 0x80

/**
 * Save index of parameter by @key attribute value
 *
 * @param paramkey_index Array to save index to (array length assumed to be MAX_PARAM_KEYS)
 * @param param_key      Value of @key attribute
 * @param param_index    Index of parameter
 */
static void save_index_of_param(uint16_t paramkey_index[], uint8_t param_key, uint16_t param_index)
{
  if (param_key &lt;= MAX_PARAM_KEY_VALUE)
  {
    <xsl:if test="$debug='true'">DBG_PRINTF("%s: paramkey_index=%p, param_key=%d, param_index=%d\n", __func__, paramkey_index, param_key, param_index);</xsl:if>
    paramkey_index[param_key] = param_index;
  }
}

/**
 * Get index of parameter with @key attribute value
 *
 * @param paramkey_index Array to get index from (array length assumed to be MAX_PARAM_KEYS)
 * @param parent_paramkey_index  The paramkey_index of the parent of a variant group
 *                       When parameter is inside a variant group it can reference
 *                       a key of the parent (see param_key description).
 * @param param_key      Value of @key attribute
 *                       If bit 0x80 is set the key should be found in the parent to
 *                       the enclosing variant group.
 *
 * @return Index of parameter or 0xFFFF if error
 */
const uint16_t get_index_of_param(const uint16_t paramkey_index[],
                                  const uint16_t parent_paramkey_index[],
                                  uint8_t param_key)
{
  uint8_t lookup_key           = param_key;
  const uint16_t *lookup_array = paramkey_index;

  if (param_key &amp; PARAMETER_KEY_BELONGS_TO_PARENT_BITFLAG)
  {
    lookup_key   = param_key &amp; ~PARAMETER_KEY_BELONGS_TO_PARENT_BITFLAG;
    lookup_array = parent_paramkey_index;
  }

  if (lookup_key &lt;= MAX_PARAM_KEY_VALUE &amp;&amp; lookup_array)
  {
    <xsl:if test="$debug='true'">DBG_PRINTF("%s: lookup_array=%p, lookup_key=%d, param_index=%d\n", __func__, lookup_array, lookup_key, lookup_array[lookup_key]);</xsl:if>
    return lookup_array[lookup_key];
  }
  else
  {
    return 0xFFFF;
  }
}

/**
 * Check a flag value stored in parameter with a specific @key
 *
 * The presence of some optional parameters is controlled by a flag stored in
 * another parameter. The @key attribute of that other parameter is referenced
 * in the XML file by the @optionaloffs attribute of the optional parameter
 * itself.
 *
 * The mapping of parameter @key attribute values to the location/index in
 * cmd[] of the actual parameter is recorded in paramkey_idx[] with
 * save_index_of_param() as each parameter of a command is processed.
 *
 * @param cmd            Command class command data
 * @param cmdLen         Length of cmd
 * @param paramkey_idx   Array mapping @key values to parameter indexes in cmd[]
 *                       Array length is assumed to be (at least) MAX_PARAM_KEYS
 * @param parent_paramkey_idx  The paramkey_index of the parent of a variant group
 *                       When the variant is inside a variant group the size_param_key
 *                       could be referencing a key of the parent.
 * @param opt_param_key  Value of @key for parameter holding the flag to check
 * @param opt_param_mask Mask to apply to parameter value to extract flag
 *
 * @return TRUE if flag is non-zero, FALSE otherwise
 */
static BOOL check_optional_param_flag(const uint8_t *cmd,
                                      int cmdLen,
                                      const uint16_t paramkey_idx[],
                                      const uint16_t parent_paramkey_idx[],
                                      uint8_t opt_param_key,
                                      uint8_t opt_param_mask
)
{
  uint16_t opt_param_idx = get_index_of_param(paramkey_idx,
                                              parent_paramkey_idx,
                                              opt_param_key);
  if (opt_param_idx &lt; cmdLen)
  {
    /* For some commands the flag that indicates if an optional parameter is
     * present is actually a multibyte value.
     * See e.g. COMMAND_CLASS_METER_v2_METER_REPORT where a non-zero value of
     * the WORD parameter "Delta Time" signals that the variant "Previous
     * Meter Value" follows.
     * But currently the attributes @optionaloffs and @optionalmask in the XML
     * file only supports single byte flag parameters and masks, so we simply
     * handle multibyte flag parameters by checking if all bytes are zero.
     */
  
    uint16_t next_param_idx = get_index_of_param(paramkey_idx,
                                                 parent_paramkey_idx,
                                                 opt_param_key + 1);

    if (next_param_idx > opt_param_idx &amp;&amp; next_param_idx &lt;= cmdLen)
    {
      uint16_t opt_param_len = next_param_idx - opt_param_idx;

      if (opt_param_len > 1)
      {
        /* Here we switch to multi-byte value checking instead of single byte flag */

        for (uint16_t idx = opt_param_idx; idx &lt; next_param_idx &amp;&amp; idx &lt; cmdLen; ++idx)
        {
          if (cmd[idx] != 0)
          {
            return TRUE; // The multibyte parameter value is non-zero
          }
        }
        return FALSE; // The multibyte parameter value is zero
      }
    }

    /* Now we can continue to do the regular single byte flag check */

    uint8_t opt_param_flag_val = cmd[opt_param_idx] &amp; opt_param_mask;
    if (opt_param_flag_val != 0)
    {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Simple length validation of a variant parameter
 *
 * @param cmd            Command class command data
 * @param cmdLen         Length of cmd
 * @param paramkey_idx   Array mapping @key values to parameter indexes in cmd[]
 *                       Array length is assumed to be (at least) MAX_PARAM_KEYS
 * @param parent_paramkey_idx  The paramkey_index of the parent of a variant group
 *                       When the variant is inside a variant group the size_param_key
 *                       could be referencing a key of the parent.
 * @param passed         Pointer to variable to increment by length of this variant
 * @param size_param_key Value of @key for parameter holding the size of the variant
 * @param size_mask      Mask to apply to the size byte to extract the variant size
 * @param size_shift     Number of bits to right shift the masked size
 * @param size_adjust    Fixed adjustment to size value (signed)
 *
 * @return PARSE_OK if variant size could be calculated, PARSE_FAIL otherwise
 */
static validator_result_t validate_variant(const uint8_t *cmd,
                                           int cmdLen,
                                           const uint16_t paramkey_idx[],
                                           const uint16_t parent_paramkey_idx[],
                                           uint16_t *passed,
                                           uint8_t size_param_key,
                                           uint8_t size_mask,
                                           uint8_t size_shift,
                                           int size_adjust)
{
  uint16_t size_param_idx = get_index_of_param(paramkey_idx,
                                               parent_paramkey_idx,
                                               size_param_key);
  if (size_param_idx &lt; cmdLen)
  {
    uint8_t size = (cmd[size_param_idx] &amp; size_mask) >> size_shift;
    *passed += size + size_adjust;
    return PARSE_OK;
  }
  else
  {
    // Error: Index of variant size parameter is outside command data buffer
    return PARSE_FAIL;
  }
}

	<xsl:for-each select="zw_classes/cmd_class">
	<xsl:for-each select="./cmd">
	<xsl:call-template name="cmd_validator"/>
	</xsl:for-each>
	</xsl:for-each>
	
	<xsl:for-each select="zw_classes/cmd_class">
static validator_result_t <xsl:value-of select="./@name"/>_v<xsl:value-of select="./@version"/>_validator(uint8_t* cmd, int cmdLen )
{
	switch( cmd[1] ) {
	<xsl:for-each select="./cmd">
	case  <xsl:value-of select="./@key"/>: /* <xsl:value-of select="./@name"/> */
		return <xsl:value-of select="../@name"/>_v<xsl:value-of select="../@version"/>_<xsl:value-of select="./@name"/>_validator(cmd+2,cmdLen-2);</xsl:for-each>
	}
	return UNKNOWN_COMMAND;
}
	</xsl:for-each>


validator_result_t ZW_command_validator(uint8_t* cmd, int cmdLen, int* version  ) {
	if(cmdLen &lt; 2) return PARSE_FAIL;
	validator_result_t result = PARSE_FAIL;
	validator_result_t result_temp = PARSE_FAIL;

	switch( cmd[0] ) {	
	<xsl:for-each select="$cmd_classes">	
	case  <xsl:value-of select="./@key"/>:
		<xsl:variable name="class" select="./@key"/>
		<xsl:for-each select="/zw_classes/cmd_class[@key = $class]">
		result_temp = <xsl:value-of select="./@name"/>_v<xsl:value-of select="./@version"/>_validator(cmd,cmdLen) ;
		if(result_temp == PARSE_OK) {
			*version = <xsl:value-of select="./@version"/>;
			result = PARSE_OK;
		}		
		</xsl:for-each>
		/*If we have succseeded  in parsing at least one version return then its ok.*/
		if(result == PARSE_OK) {
			return PARSE_OK;
		}
		return result_temp;
	</xsl:for-each>

	}

	return UNKNOWN_CLASS;
}

	</xsl:template>

</xsl:stylesheet>
