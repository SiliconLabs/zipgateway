"""Copyright 2019 Silicon Laboratories Inc.

Script that generates doxygen documentation for
json schemas.
It is implemented to support the json schemas in systools/doc.
"""
from __future__ import print_function
import os
import sys
import json
import argparse
import collections


def eprint(*args, **kwargs):
    """
    Print to stderr
    """
    print(*args, file=sys.stderr, **kwargs)


class ZwDocumentationHelper(object):
    """
    Helper Class for generating documentation

    Can generate language specific links, headers, etc.
    """
    def __init__(self, html_page_link, link_prepend=""):
        """Constructor

        Arguments:
            html_page_link {str} -- Link to HTML page, where documentation is
                                    included.
                                    This is used when making links in plantuml

        Keyword Arguments:
            link_prepend {str} -- prepended to all links (default: {""})
        """
        self.link_prepend = link_prepend
        self.html_page_link = html_page_link

    def get_link(self, name):
        return self.link_prepend + name.replace('/', '_')

    def get_plantuml_link(self, name):
        return "{}#{}".format(self.html_page_link,
                              self.get_link(name))

    def get_header(self, level, name, linkname=None):
        if linkname is None:
            linkname = name
        header = ""
        if level == 0:
            header += "\\page"
        elif level == 1:
            header += "\\section"
        elif level == 2:
            header += "\\subsection"
        elif level == 3:
            header += "\\subsubsection"
        elif level == 4:
            header += "\\paragraph"
        else:
            return "\\b {}\n\n".format(name)
        return "{} {} {}\n".format(header,
                                   self.get_link(linkname),
                                   name)

    def get_reference(self, linkname, text):
        return "\\ref {} \"{}\"".format(self.get_link(linkname), text)


class ZwJsonDefinition(object):
    """
    Wrapper for a Definition in the json schema
    """
    def __init__(self, name, json_obj, doc_helper):
        """Constructor

        Arguments:
            name {str} -- Name of the Definition
            json_obj {dict} -- The json object, from the passed json
            doc_helper {ZwDocumentationHelper} -- Documenation helper
        """
        self.name = name
        self.json_obj = json_obj
        if 'type' in json_obj:
            self.type = json_obj['type']
        elif 'oneOf' in json_obj:
            self.type = "oneOf"
        else:
            raise NotImplementedError()
        self._dh = doc_helper

    def _obj_to_kv_table(self, obj):
        """
        Generate Key Value table of elemets in the object
        """
        string = ("<table>\n"
                  "  <tr>\n"
                  "    <th>Key</th>\n"
                  "    <th>Value</th>\n"
                  "  </tr>\n")

        for element in obj:
            if isinstance(obj[element], dict):
                str_element = ""
                str_element = "\n      <ul>\n"
                for el in obj[element]:
                    if el == "$ref":
                        short_type = obj[element]['$ref'].split('/')[-1]
                        tmp_str = self._dh.get_reference(
                                            "definitions_" + short_type,
                                            short_type)
                    else:
                        tmp_str = str(el)
                    str_element += "        <li>{}</li>\n".format(tmp_str)
                str_element += "      </ul>\n      "
            elif isinstance(obj[element], list):
                str_element = "\n      <ul>\n"
                for el in obj[element]:
                    str_element += "        <li>{}</li>\n".format(str(el))
                str_element += "      </ul>\n      "
            else:
                if element == "pattern":
                    str_element = "\code{.unparsed}" + str(obj[element]) + "\endcode"
                else:
                    str_element = str(obj[element])
            string += ("  <tr>\n"
                       "    <td>{key}</td>\n"
                       "    <td>{value}</td>\n"
                       "  </tr>\n").format(key=element, value=str_element)
        string += ("</table>\n")
        return string

    def _obj_to_str(self, json_obj):
        string = ""
        if 'oneOf' in json_obj:
            string += "One Of:"
            string += "<ol>\n"
            for one_of in json_obj['oneOf']:
                string += "<li>\n"
                string += self._obj_to_str(one_of)
                string += "</li>\n"
            string += "</ol>\n"
        else:
            string += self._obj_to_kv_table(json_obj)

        if 'properties' in json_obj:
            for prop in json_obj['properties']:
                string += self._dh.get_header(5, prop)
                string += self._obj_to_str(json_obj['properties'][prop])
                string += "\n"
            self._obj_to_str(json_obj['properties'])
        string += "\n"
        return string

    def as_type(self):
        """
        Get the Definition as string of type
        specified in the ZwDocumentationHelper

        Returns:
            string -- String representation of the Definition
        """
        string = self._dh.get_header(3, self.name, "definitions_" + self.name)
        string += "\n"
        string += self._obj_to_str(self.json_obj)
        return string


class ZwJsonDefinitions(object):
    """
    Container class for definitions
    """
    def __init__(self):
        self.definitions = []

    def append(self, definition):
        self.definitions.append(definition)

    def as_tree(self):
        string = ""
        for definition in self.definitions:
            string += definition.as_type()
        string += "\n"
        return string


class ZwJsonObj(object):
    """Wrapping a json object with children from a json schema"""
    def __init__(self, name, namespace, json_obj, doc_helper, required,
                 condition):
        """Constructor for a ZwJsonObj

            name {str} -- Name of the object
            namespace {str} -- Namespace for the object
            json_obj {dict} -- The Json object to be wrapped
            doc_helper {ZwDocumentationHelper} -- Documenation helper
        """
        self.name = name
        self.namespace = namespace
        self.json_obj = json_obj
        self._dh = doc_helper
        self.enum = None
        self._set_type(json_obj)
        if 'default' in json_obj:
            self.default = str(json_obj["default"])
        else:
            self.default = ""
        self.required = required
        self.children = []
        self.condition = condition

    def _set_type(self, json_obj):
        if 'enum' in json_obj:
            self.type = str(json_obj['type']) + " (enum)"
            self.short_type = self.type
            self.enum = json_obj['enum']
        elif 'type' in json_obj:
            self.type = json_obj['type']
            self.short_type = self.type
        elif '$ref' in json_obj:
            self.short_type = json_obj['$ref'].split('/')[-1]
            self.type = self._dh.get_reference(
                "definitions_" + self.short_type, self.short_type)
        else:
            self.type = "None"
            self.short_type = self.type

        if self.type == 'array':
            if 'type' in self.json_obj['items']:
                self.array_item_type = self.json_obj['items']['type']
                self.array_item_short_type = self.array_item_type
            elif '$ref' in self.json_obj['items']:
                self.array_item_short_type = json_obj['items']['$ref'].split(
                                                                    '/')[-1]
                self.array_item_type = self._dh.get_reference(
                        "definitions_" + self.array_item_short_type,
                        self.array_item_short_type)

    def get_type_str(self, with_ref=True):
        if self.type == 'array':
            if with_ref:
                return "{} [{}]".format(self.type,
                                        self.array_item_type)
            else:
                return "{} [{}]".format(self.short_type,
                                        self.array_item_short_type)
        else:
            if with_ref:
                return self.type
            else:
                return self.short_type

    def add_child(self, child):
        """Add child to the ZwJsonObj

            child {ZwJsonObj} -- A child of same type as the ZwJsonObj
        """
        self.children.append(child)

    def get_linkname(self):
        return self.namespace + self.name

    def as_doc_type(self):
        """Generate a string representation of the object

        Returns:
            {string} -- String containg the representation of the ZwJsonObj
        """
        linkname = self.get_linkname()
        string = self._dh.get_header(3, linkname)
        string += "\n"
        string += self._dh.get_header(4, "Description",
                                      linkname + "/desc")
        string += "\n"
        if 'description' in self.json_obj:
            string += self.json_obj['description']
            string += "\n"
        string += ("<table>\n"
                   "  <tr>\n"
                   "    <th>Type</th>\n"
                   "    <th>Required</th>\n"
                   "    <th>Default</th>\n"
                   "  </tr>\n")
        string += ("  <tr>\n"
                   "    <td>{}</td>\n"
                   "    <td>{}</td>\n"
                   "    <td>{}</td>\n"
                   "  </tr>\n").format(self.get_type_str(),
                                       str(self.required),
                                       self.default)
        string += "</table>\n"

        if self.enum:
            string += self._dh.get_header(4, "Enum values",
                                          linkname + "/enum")
            string += "<ol>\n"
            for en in self.enum:
                string += "  <li>{}</li>".format(en)
            string += "</ol>\n"

        if len(self.children) > 0:
            string += self._dh.get_header(4, "Properties",
                                          linkname + "/prop")
            string += "\n"
            string += self._create_property_table(self.children)
            string += "\n"
        string += "\n"
        return string

    def _create_property_table(self, obj):
        string = ("<table>\n"
                  "  <tr>\n"
                  "    <th>Property</th>\n"
                  "    <th>Required</th>\n"
                  "    <th>Condition</th>\n"
                  "  </tr>\n")
        for child in obj:
            child_link = self._dh.get_reference(child.get_linkname(),
                                                child.name)
            string += ("  <tr>\n"
                       "    <td>{}</td>\n"
                       "    <td>{}</td>\n"
                       "    <td>{}</td>\n"
                       "  </tr>\n").format(child_link,
                                           str(child.required),
                                           child.condition)
        string += "</table>\n"
        return string

    def as_tree(self, depth=None):
        """Generate a string representation of the ZwJsonObj and its children

            depth {int} -- If set, this will be the depth to traverse
                               in the tree of children (default: {None})
        Returns:
            {string} -- String representation of the ZwJsonObj and its children
        """
        string = self.as_doc_type()
        if depth is None or depth > 0:
            for child in self.children:
                string += child.as_tree(None if depth is None else depth - 1)
        return string

    def as_plantuml(self):
        string = "class \"{}\"".format(self.name)
        pu_link_name = self.get_linkname().replace('/', '.')
        if self.name != pu_link_name:
            string += " as {} ".format(pu_link_name)
        string += " [[{}]]".format(
                            self._dh.get_plantuml_link(self.get_linkname()))
        string += "{\n"
        string += "    type: {}\n".format(self.get_type_str(False))
        string += "    .. Members ..\n"
        for child in self.children:
            string += "    {}: {}: [{}]\n".format(
                                        "+" if child.required else "-",
                                        child.name,
                                        child.get_type_str(False))
        string += "}\n\n"
        return string


class JsonSchema(object):
    """Wrapper of a Json Schema file"""
    def __init__(self, path, doc_helper):
        self._path = path
        self._dh = doc_helper
        with open(path, 'r') as json_file:
            self._json = json.load(json_file,
                                   object_pairs_hook=collections.OrderedDict)
            self.zw_json_objs = []
            self.definitions = ZwJsonDefinitions()
        self._parse_definitions()
        self._parse_objects()

    def print_definitions(self):
        for _def in self._json['definitions']:
            print(_def)

    def _parse_definitions(self):
        for _def in self._json['definitions']:
            self.definitions.append(ZwJsonDefinition(_def,
                                    self._json['definitions'][_def],
                                    self._dh))

    def _print_obj_str(self, _obj, namespace, key):
        level = len(namespace.split('/'))
        string = "{}{}".format(namespace, key)
        if 'type' in _obj:
            string += ", Type: {}".format(_obj['type'])
        elif '$ref' in _obj:
            string += ", Type: {}".format(_obj['$ref'].split('/')[-1])
        if 'description' in _obj:
            string += ", Description: {}".format(_obj['description'])
        print(string.rjust(len(string) + level*2))

    def _parse_objects(self, obj_in=None, parent=None, condition=""):
        if obj_in is None:
            obj_in = self._json
        obj = None
        if 'properties' in obj_in:
            obj = obj_in['properties']
        elif 'items' in obj_in and 'properties' in obj_in['items']:
            obj = obj_in['items']['properties']

        if parent is None:
            namespace = ""
        else:
            namespace = parent.namespace + parent.name + '/'
        if obj:
            for key in obj:
                _obj = obj[key]
                if ('required' in obj_in and key in obj_in['required']):
                    required = True
                else:
                    required = False
                zw_obj = ZwJsonObj(key, namespace, _obj, self._dh, required,
                                   condition)
                if parent:
                    parent.add_child(zw_obj)
                else:
                    self.zw_json_objs.append(zw_obj)
                self._parse_objects(_obj, zw_obj)
        condition = ""
        if 'if' in obj_in:
            if 'properties' in obj_in['if']:
                for key, value in obj_in['if']['properties'].items():
                    condition = "{} == {}".format(key, value)
        if 'then' in obj_in:
            self._parse_objects(obj_in['then'], parent, condition)
        if 'else' in obj_in:
            self._parse_objects(obj_in['else'], parent,
                                "NOT ({})".format(condition))

    def as_doc_type(self):
        string = self._dh.get_header(2, "Definitions")
        string += "\n"
        string += schema.definitions.as_tree()
        string += "\n"
        string += self._dh.get_header(2, "Objects")

        string += "\n"
        for zw_obj in schema.zw_json_objs:
            string += zw_obj.as_tree()
        return string

    def _get_plantuml_links_recursive(self, obj):
        string = ""
        for child in obj.children:
            if child.type == "object" or child.type == "array":
                string += "{} -- {}\n".format(
                        obj.get_linkname().replace('/', '.'),
                        child.get_linkname().replace('/', '.'))
                string += self._get_plantuml_links_recursive(child)
        return string

    def _get_plantuml_classes_recursive(self, obj):
        string = obj.as_plantuml()
        for child in obj.children:
            if child.type == "object" or child.type == "array":
                string += self._get_plantuml_classes_recursive(child)
        return string

    def as_plantuml(self):
        """Generate Plantuml representation"""
        string = "\\startuml\n"
        # Plantuml configuration
        string += ("hide circle\n"
                   "skinparam classAttributeIconSize 0\n")

        for zw_obj in schema.zw_json_objs:
            string += self._get_plantuml_classes_recursive(zw_obj)
            string += self._get_plantuml_links_recursive(zw_obj)
        string += "\\enduml\n\n"
        return string


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate Doxygen documentation for a Json Schema file")
    parser.add_argument("input", help="Path to Json Schema file")
    parser.add_argument("output", help="Path out destination file")

    parser.add_argument("--pu",
                        action="store_true",
                        dest="pu",
                        default=False,
                        help="Generate Plantuml in doxygen")
    parser.add_argument("--html_page",
                        help=("Link to HTML page where generated documentation"
                              " is included"),
                        default="page_systools.html")
    p_args = parser.parse_args()

    dh = ZwDocumentationHelper(p_args.html_page,
                               p_args.input.split('/')[-1].split('.')[0] + "_")

    schema = JsonSchema(p_args.input, dh)
    output = ""
    if p_args.pu:
        output = schema.as_plantuml()
    output += schema.as_doc_type()
    if sys.version_info < (3, 0):  # Python2
        output = output.encode('utf-8')
    if p_args.output.lower() == "stdout".lower():
        print(output)
    else:
        with open(p_args.output, "w") as out_file:
            out_file.write(output)
