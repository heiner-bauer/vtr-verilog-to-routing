/*
See vpr/SRC/route/build_switchblocks.c for a detailed description of how the new
switch block format works and what files are involved.

 
A large chunk of this file is dedicated to helping parse the initial switchblock
specificaiton in the XML arch file, providing error checking, etc.

Another large chunk of this file is dedicated to parsing the actual formulas 
specified by the switch block permutation functions into their numeric counterparts.
*/


#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include <stack>
#include <utility>
#include <algorithm>

#include "vtr_assert.h"
#include "vtr_util.h"

#include "pugixml.hpp"
#include "pugixml_util.hpp"

#include "arch_error.h"

#include "read_xml_util.h"
#include "arch_util.h"
#include "arch_types.h"
#include "physical_types.h"
#include "parse_switchblocks.h"


using namespace std;
using namespace pugiutil;

/**** Enums ****/
/* Used to identify the type of symbolic formula object */
typedef enum e_formula_obj{
	E_FML_UNDEFINED = 0,
	E_FML_NUMBER,
	E_FML_BRACKET,
	E_FML_OPERATOR,
	E_FML_NUM_FORMULA_OBJS
} t_formula_obj;


/* Used to identify an operator in a formula */ 
typedef enum e_operator{
	E_OP_UNDEFINED = 0,
	E_OP_ADD,
	E_OP_SUB,
	E_OP_MULT,
	E_OP_DIV,
	E_OP_NUM_OPS
} t_operator;


/**** Class Definitions ****/
/* This class is used to represent an object in a formula, such as 
   a number, a bracket, an operator, or a variable */
class Formula_Object{
public:
	/* indicates the type of formula object this is */
	t_formula_obj type;

	/* object data, accessed based on what kind of object this is */
	union u_Data {
		int num;		/*for number objects*/
		t_operator op;		/*for operator objects*/
		bool left_bracket;	/*for bracket objects -- specifies if this is a left bracket*/

		u_Data(){ memset(this, 0, sizeof(u_Data)); }
	} data;

	Formula_Object(){
		this->type = E_FML_UNDEFINED;
	}
	
};


/**** Function Declarations ****/
/*---- Functions for Parsing Switchblocks from Architecture ----*/

//Load an XML wireconn specification into a t_wireconn_inf 
t_wireconn_inf parse_wireconn(pugi::xml_node node, const pugiutil::loc_data& loc_data);

//Process a wireconn defined in the inline style (using attributes)
void parse_wireconn_inline(pugi::xml_node node, const pugiutil::loc_data& loc_data, t_wireconn_inf& wc);

//Process a wireconn defined in the multinode style (more advanced specification)
void parse_wireconn_multinode(pugi::xml_node node, const pugiutil::loc_data& loc_data, t_wireconn_inf& wc);

//Process a <from> or <to> sub-node of a multinode wireconn
t_wire_switchpoints parse_wireconn_from_to_node(pugi::xml_node node, const pugiutil::loc_data& loc_data);

/* parses the wire types specified in the comma-separated 'ch' char array into the vector wire_points_vec. 
   Spaces are trimmed off */
static void parse_comma_separated_wire_types(const char *ch, std::vector<t_wire_switchpoints>& wire_switchpoints);

/* parses the wirepoints specified in ch into the vector wire_points_vec */
static void parse_comma_separated_wire_points(const char *ch, std::vector<t_wire_switchpoints>& wire_switchpoints);

/* Parses the number of connections type */
static void parse_num_conns(std::string num_conns, t_wireconn_inf& wireconn);

/* checks for correctness of a unidir switchblock. */
static void check_unidir_switchblock(const t_switchblock_inf *sb );

/* checks for correctness of a bidir switchblock. */
static void check_bidir_switchblock(const t_permutation_map *permutation_map );

/* checks for correctness of a wireconn segment specification. */
static void check_wireconn(const t_arch* arch, const t_wireconn_inf& wireconn);

/*---- Functions for Parsing the Symbolic Switchblock Formulas ----*/
/* returns integer result according to specified formula and data */
static int parse_formula( const char *formula, const t_formula_data &mydata );

/* returns integer result according to specified piece-wise formula and data */
static int parse_piecewise_formula( const char *formula, const t_formula_data &mydata );

/* converts specified formula to a vector in reverse-polish notation */
static void formula_to_rpn( const char* formula, const t_formula_data &mydata, 
				vector<Formula_Object> &rpn_output );

static void get_formula_object( const char *ch, int &ichar, const t_formula_data &mydata,
				 Formula_Object *fobj );

/* returns integer specifying precedence of passed-in operator. higher integer 
   means higher precedence */
static int get_fobj_precedence( const Formula_Object &fobj );

/* Returns associativity of the specified operator */
static bool op_associativity_is_left( const t_operator &op );

/* used by the shunting-yard formula parser to deal with operators such as add and subtract */
static void handle_operator( const Formula_Object &fobj, vector<Formula_Object> &rpn_output,
				stack<Formula_Object> &op_stack);

/* used by the shunting-yard formula parser to deal with brackets, ie '(' and ')' */
static void handle_bracket( const Formula_Object &fobj, vector<Formula_Object> &rpn_output,
				stack<Formula_Object> &op_stack);

/* parses revere-polish notation vector to return formula result */
static int parse_rpn_vector( vector<Formula_Object> &rpn_vec );

/* applies operation specified by 'op' to the given arguments. arg1 comes before arg2 */
static int apply_rpn_op( const Formula_Object &arg1, const Formula_Object &arg2, 
					const Formula_Object &op );

/* checks if specified character represents an ASCII number */
static bool is_char_number( const char ch );

/* checks if the specified formula is piece-wise defined */
static bool is_piecewise_formula( const char *formula );

/* increments str_ind until it reaches specified char is formula. returns true if character was found, false otherwise */
static bool goto_next_char( int *str_ind, const string &pw_formula, char ch);


/**** Function Definitions ****/

/*---- Functions for Parsing Switchblocks from Architecture ----*/

/* Reads-in the wire connections specified for the switchblock in the xml arch file */
void read_sb_wireconns(const t_arch_switch_inf * /*switches*/, int /*num_switches*/, pugi::xml_node Node, t_switchblock_inf *sb, const pugiutil::loc_data& loc_data ){
	
	/* Make sure that Node is a switchblock */
	check_node(Node, "switchblock", loc_data);
	
	int num_wireconns;
	pugi::xml_node SubElem;

	/* count the number of specified wire connections for this SB */
	num_wireconns = count_children(Node, "wireconn", loc_data, OPTIONAL);
	sb->wireconns.reserve(num_wireconns);

	if (num_wireconns > 0) {
		SubElem = get_first_child(Node, "wireconn", loc_data);	
	}
	for (int i = 0; i < num_wireconns; i++){
        t_wireconn_inf wc = parse_wireconn(SubElem, loc_data);
		sb->wireconns.push_back(wc);
		SubElem = SubElem.next_sibling(SubElem.name());
	}

	return;
}

t_wireconn_inf parse_wireconn(pugi::xml_node node, const pugiutil::loc_data& loc_data) {
    t_wireconn_inf wc;

    size_t num_attributes = count_attributes(node, loc_data);

    if (num_attributes == 1) {
        parse_wireconn_multinode(node, loc_data, wc);
    } else {
        VTR_ASSERT(num_attributes > 0);
        parse_wireconn_inline(node, loc_data, wc);
    }


    return wc;
}

void parse_wireconn_inline(pugi::xml_node node, const pugiutil::loc_data& loc_data, t_wireconn_inf& wc) {
    //Parse an inline wireconn definition, using attributes

    /* get the connection style */
    const char* char_prop = get_attribute(node, "num_conns_type", loc_data).value();
    parse_num_conns(char_prop, wc);

    /* get from type */
    char_prop = get_attribute(node, "from_type", loc_data).value();
    parse_comma_separated_wire_types(char_prop, wc.from_switchpoint_set);

    /* get to type */
    char_prop = get_attribute(node, "to_type", loc_data).value();
    parse_comma_separated_wire_types(char_prop, wc.to_switchpoint_set);

    /* get the source wire point */
    char_prop = get_attribute(node, "from_switchpoint", loc_data).value();
    parse_comma_separated_wire_points(char_prop, wc.from_switchpoint_set);

    /* get the destination wire point */
    char_prop = get_attribute(node, "to_switchpoint", loc_data).value();
    parse_comma_separated_wire_points(char_prop, wc.to_switchpoint_set);
}

void parse_wireconn_multinode(pugi::xml_node node, const pugiutil::loc_data& loc_data, t_wireconn_inf& wc) {
    /* get the connection style */
    const char* char_prop = get_attribute(node, "num_conns_type", loc_data).value();
    parse_num_conns(char_prop, wc);

    size_t num_from_children = count_children(node, "from", loc_data);
    size_t num_to_children = count_children(node, "to", loc_data);

    VTR_ASSERT(num_from_children > 0);
    VTR_ASSERT(num_to_children > 0);

    for (pugi::xml_node child : node.children()) {
        if (child.name() == std::string("from")) {
            t_wire_switchpoints from_switchpoints = parse_wireconn_from_to_node(child, loc_data);
            wc.from_switchpoint_set.push_back(from_switchpoints);
        } else if (child.name() == std::string("to")) {
            t_wire_switchpoints to_switchpoints = parse_wireconn_from_to_node(child, loc_data);
            wc.to_switchpoint_set.push_back(to_switchpoints);
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(node), "Unrecognized child node '%s' of parent node '%s'",
                            node.name(), child.name());
        }
    }
}

t_wire_switchpoints parse_wireconn_from_to_node(pugi::xml_node node, const pugiutil::loc_data& loc_data) {
    size_t attribute_count = count_attributes(node, loc_data);

    if (attribute_count != 2) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node), "Expected only 2 attributes on node '%s'",
                        node.name());
    }

    t_wire_switchpoints wire_switchpoints;
    wire_switchpoints.segment_name = get_attribute(node, "type", loc_data).value();

    auto points_str = get_attribute(node, "switchpoint", loc_data).value();
    for (auto point_str : vtr::split(points_str, ",")) {
        int switchpoint = vtr::atoi(point_str);
        wire_switchpoints.switchpoints.push_back(switchpoint);
    }

    if (wire_switchpoints.switchpoints.empty()) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node), "Empty switchpoint specification",
                        node.name());
    }

    return wire_switchpoints;
}

/* parses the wire types specified in the comma-separated 'ch' char array into the vector wire_points_vec. 
   Spaces are trimmed off */
static void parse_comma_separated_wire_types(const char *ch, std::vector<t_wire_switchpoints>& wire_switchpoints) {
    auto types = vtr::split(ch, ",");

    if (types.empty()){
        archfpga_throw( __FILE__, __LINE__, "parse_comma_separated_wire_types: found empty wireconn wire type entry\n");
    }

    for (auto type : types) {
        t_wire_switchpoints wsp;
        wsp.segment_name = type;

        wire_switchpoints.push_back(wsp);
    }
}


/* parses the wirepoints specified in the comma-separated 'ch' char array into the vector wire_points_vec */
static void parse_comma_separated_wire_points(const char *ch, std::vector<t_wire_switchpoints>& wire_switchpoints){
    auto points = vtr::split(ch, ",");
    if (points.empty()){
        archfpga_throw( __FILE__, __LINE__, "parse_comma_separated_wire_points: found empty wireconn wire point entry\n");
    }

    for(auto point_str : points) {
        int point = vtr::atoi(point_str);

        for(auto& wire_switchpoint : wire_switchpoints) {
            wire_switchpoint.switchpoints.push_back(point);
        }
    }
}

static void parse_num_conns(std::string num_conns, t_wireconn_inf& wireconn) {

    if (num_conns == "from") {
        wireconn.num_conns_type = WireConnType::FROM;
    } else if (num_conns == "to") {
        wireconn.num_conns_type = WireConnType::TO;
    } else if (num_conns == "min") {
        wireconn.num_conns_type = WireConnType::MIN;
    } else if (num_conns == "max") {
        wireconn.num_conns_type = WireConnType::MAX;
    } else {
		archfpga_throw( __FILE__, __LINE__, "Invalid num_conns specification '%s'", num_conns.c_str());
    }
}

/* Loads permutation funcs specified under Node into t_switchblock_inf. Node should be 
   <switchfuncs> */
void read_sb_switchfuncs( pugi::xml_node Node, t_switchblock_inf *sb, const pugiutil::loc_data& loc_data ){
	
	/* Make sure the passed-in is correct */
	check_node(Node, "switchfuncs", loc_data);
	
	pugi::xml_node SubElem;

	/* get the number of specified permutation functions */
	int num_funcs = count_children(Node, "func", loc_data, OPTIONAL);

	const char * func_type;
	const char * func_formula;
	vector<string> * func_ptr;

	/* used to index into permutation map of switchblock */
	SB_Side_Connection conn;

	/* now we iterate through all the specified permutation functions, and 
	   load them into the switchblock structure as appropriate */
	if (num_funcs > 0) {
		SubElem = get_first_child(Node, "func", loc_data);	
	}
	for (int ifunc = 0; ifunc < num_funcs; ifunc++){

		/* get function type */
		func_type = get_attribute(SubElem, "type", loc_data).as_string(NULL);

		/* get function formula */
		func_formula = get_attribute(SubElem, "formula", loc_data).as_string(NULL);

		/* go through all the possible cases of func_type */
		if (0 == strcmp(func_type, "lt")){
			conn.set_sides(LEFT, TOP);
		} else if (0 == strcmp(func_type, "lr")) {
			conn.set_sides(LEFT, RIGHT);
		} else if (0 == strcmp(func_type, "lb")) {
			conn.set_sides(LEFT, BOTTOM);
		} else if (0 == strcmp(func_type, "tl")) {
			conn.set_sides(TOP, LEFT);
		} else if (0 == strcmp(func_type, "tb")) {
			conn.set_sides(TOP, BOTTOM);
		} else if (0 == strcmp(func_type, "tr")) {
			conn.set_sides(TOP, RIGHT);
		} else if (0 == strcmp(func_type, "rt")) {
			conn.set_sides(RIGHT, TOP);
		} else if (0 == strcmp(func_type, "rl")) {
			conn.set_sides(RIGHT, LEFT);
		} else if (0 == strcmp(func_type, "rb")) {
			conn.set_sides(RIGHT, BOTTOM);
		} else if (0 == strcmp(func_type, "bl")) {
			conn.set_sides(BOTTOM, LEFT);
		} else if (0 == strcmp(func_type, "bt")) {
			conn.set_sides(BOTTOM, TOP);
		} else if (0 == strcmp(func_type, "br")) {
			conn.set_sides(BOTTOM, RIGHT);
		} else {
			/* unknown permutation function */
			archfpga_throw( __FILE__, __LINE__, "Unknown permutation function specified: %s\n", func_type);
		}
		func_ptr = &(sb->permutation_map[conn]);

		/* Here we load the specified switch function(s) */
		func_ptr->push_back( string(func_formula) );
 
		func_ptr = NULL;
		/* get the next switchblock function */
		SubElem = SubElem.next_sibling(SubElem.name());
	}

	return;
}


/* checks for correctness of switch block read-in from the XML architecture file */
void check_switchblock(const t_switchblock_inf* sb, const t_arch* arch){

	/* get directionality */
	enum e_directionality directionality = sb->directionality;

	/* Check for errors in the switchblock descriptions */
	if (UNI_DIRECTIONAL == directionality){
		check_unidir_switchblock( sb );
	} else {
		VTR_ASSERT(BI_DIRECTIONAL == directionality);
		check_bidir_switchblock( &(sb->permutation_map) );
	}


	/* check that specified wires exist */
    for (const auto& wireconn : sb->wireconns) {

        check_wireconn(arch, wireconn);
    }

	//TODO:
	/* check that the wire segment directionality matches the specified switch block directionality */
	/* check for duplicate names */
	/* check that specified switches exist */
	/* check that type of switchblock matches type of switch specified */
}


/* checks for correctness of a unidirectional switchblock. hard exit if error found (to be changed to throw later) */
static void check_unidir_switchblock(const t_switchblock_inf *sb ){

	/* Check that the destination wire points are always the starting points (i.e. of wire point 0) */
	for (const t_wireconn_inf& wireconn : sb->wireconns){
        for (const t_wire_switchpoints& wire_to_points : wireconn.to_switchpoint_set) {
            if (wire_to_points.switchpoints.size() > 1 || wire_to_points.switchpoints[0] != 0){
                archfpga_throw( __FILE__, __LINE__, "Unidirectional switch blocks are currently only allowed to drive the start points of wire segments\n");
            }
        }
	}
}


/* checks for correctness of a bidirectional switchblock */
static void check_bidir_switchblock(const t_permutation_map *permutation_map ){
	/**** check that if side1->side2 is specified, then side2->side1 is not, as it is implicit ****/

	/* variable used to index into the permutation map */
	SB_Side_Connection conn;

	/* iterate over all combinations of from_side -> to side */
	for (e_side from_side : {TOP, RIGHT, BOTTOM, LEFT}) {
		for (e_side to_side : {TOP, RIGHT, BOTTOM, LEFT}) {
			/* can't connect a switchblock side to itself */
			if (from_side == to_side){
				continue;
			}

			/* index into permutation map with this variable */			
			conn.set_sides(from_side, to_side);

			/* check if a connection between these sides exists */
			t_permutation_map::const_iterator it = (*permutation_map).find(conn);
			if (it != (*permutation_map).end()){
				/* the two sides are connected */
				/* check if the opposite connection has been specified */
				conn.set_sides(to_side, from_side);
				it = (*permutation_map).find(conn);
				if (it != (*permutation_map).end()){
					archfpga_throw( __FILE__, __LINE__, "If a bidirectional switch block specifies a connection from side1->side2, no connection should be specified from side2->side1 as it is implicit.\n");
				}
			}
		}
	}

	return;
}

static void check_wireconn(const t_arch* arch, const t_wireconn_inf& wireconn) {
    for (const t_wire_switchpoints& wire_switchpoints : wireconn.from_switchpoint_set) {
        auto seg_name = wire_switchpoints.segment_name;

        //Make sure the segment exists
        const t_segment_inf* seg_info = find_segment(arch, seg_name);
        if (!seg_info) {
            archfpga_throw( __FILE__, __LINE__, "Failed to find segment '%s' for <wireconn> from type specification\n", seg_name.c_str());
        }

        //Check that the specified switch points are valid
        for(int switchpoint : wire_switchpoints.switchpoints) {
            if (switchpoint < 0) {
                archfpga_throw( __FILE__, __LINE__, "Invalid <wireconn> from_switchpoint '%d' (must be >= 0)\n", switchpoint, seg_name.c_str());
            }
            if (switchpoint >= seg_info->length) {
                archfpga_throw( __FILE__, __LINE__, "Invalid <wireconn> from_switchpoints '%d' (must be < %d)\n", switchpoint, seg_info->length);
            }
            //TODO: check that points correspond to valid sb locations
        }
    }

    for (const t_wire_switchpoints& wire_switchpoints : wireconn.to_switchpoint_set) {
        auto seg_name = wire_switchpoints.segment_name;

        //Make sure the segment exists
        const t_segment_inf* seg_info = find_segment(arch, seg_name);
        if (!seg_info) {
            archfpga_throw( __FILE__, __LINE__, "Failed to find segment '%s' for <wireconn> to type specification\n", seg_name.c_str());
        }

        //Check that the specified switch points are valid
        for(int switchpoint : wire_switchpoints.switchpoints) {
            if (switchpoint < 0) {
                archfpga_throw( __FILE__, __LINE__, "Invalid <wireconn> to_switchpoint '%d' (must be >= 0)\n", switchpoint, seg_name.c_str());
            }
            if (switchpoint >= seg_info->length) {
                archfpga_throw( __FILE__, __LINE__, "Invalid <wireconn> to_switchpoints '%d' (must be < %d)\n", switchpoint, seg_info->length);
            }
            //TODO: check that points correspond to valid sb locations
        }
    }

}

/*---- Functions for Parsing the Symbolic Switchblock Formulas ----*/

/* returns integer result according to the specified switchblock formula and data. formula may be piece-wise */
int get_sb_formula_raw_result( const char* formula, const t_formula_data &mydata ){
	/* the result of the formula will be an integer */
	int result = -1;

	/* check formula */
	if (NULL == formula){
		archfpga_throw( __FILE__, __LINE__, "in get_sb_formula_result: SB formula pointer NULL\n");
	} else if ('\0' == formula[0]){
		archfpga_throw( __FILE__, __LINE__, "in get_sb_formula_result: SB formula empty\n");
	}

	/* parse based on whether formula is piece-wise or not */
	if ( is_piecewise_formula(formula) ){
		//EXPERIMENTAL
		result = parse_piecewise_formula( formula, mydata );
	} else {
		result = parse_formula( formula, mydata );
	}
	
	return result;
}


/* returns integer result according to specified non-piece-wise formula and data */
static int parse_formula( const char *formula, const t_formula_data &mydata ){
	int result = -1;

	/* output in reverse-polish notation */
	vector<Formula_Object> rpn_output;	

	/* now we have to run the shunting-yard algorithm to convert formula to reverse polish notation */
	formula_to_rpn( formula, mydata, rpn_output );
	
	/* then we run an RPN parser to get the final result */
	result = parse_rpn_vector(rpn_output);
	
	return result;
}


/* EXPERIMENTAL:
   
   returns integer result according to specified piece-wise formula and data. the piecewise 
   notation specifies different formulas that should be evaluated based on the index of 
   the incoming wire in 'mydata'. for example the formula 

       {0:(W/2)} t-1; {(W/2):W} t+1;

   indicates that the function "t-1" should be evaluated if the incoming wire index falls 
   within the range [0,W/2) and that "t+1" should be evaluated if it falls within the 
   [W/2,W) range. The piece-wise format is:
   
       {start_0:end_0} formula_0; ... {start_i;end_i} formula_i; ... 
*/
static int parse_piecewise_formula( const char *formula, const t_formula_data &mydata ){
	int result = -1;
	int str_ind = 0;
	int str_size = 0;
	int t = mydata.wire;
	int tmp_ind_start = -1;
	int tmp_ind_count = -1;
	string substr;
	
	/* convert formula to string format */
	string pw_formula(formula);
	str_size = pw_formula.size();

	if (pw_formula[str_ind] != '{'){
		archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: the first character in piece-wise formula should always be '{'\n");
	}
	
	/* find the range to which t corresponds */
	/* the first character must be '{' as verified above */
	while (str_ind != str_size - 1){
		/* set to true when range to which wire number corresponds has been found */
		bool found_range = false;
		bool char_found = false;
		int range_start = -1;
		int range_end = -1;
		tmp_ind_start = -1;
		tmp_ind_count = -1;

		/* get the start of the range */
		tmp_ind_start = str_ind + 1;
		char_found = goto_next_char(&str_ind, pw_formula, ':');
		if (!char_found){
			archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: could not find char %c\n", ':');
		}
		tmp_ind_count = str_ind - tmp_ind_start;			/* range start is between { and : */
		substr = pw_formula.substr(tmp_ind_start, tmp_ind_count);
		range_start = parse_formula(substr.c_str(), mydata);
	
		/* get the end of the range */
		tmp_ind_start = str_ind + 1;
		char_found = goto_next_char(&str_ind, pw_formula, '}');
		if (!char_found){
			archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: could not find char %c\n", '}');
		}
		tmp_ind_count = str_ind - tmp_ind_start;			/* range end is between : and } */
		substr = pw_formula.substr(tmp_ind_start, tmp_ind_count);
		range_end = parse_formula(substr.c_str(), mydata);

		if (range_start > range_end){
			archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: range_start, %d, is bigger than range end, %d\n", range_start, range_end);
		}

		/* is the incoming wire within this range? (inclusive) */
		if ( range_start <= t && range_end >= t ){
			found_range = true;
		} else {
			found_range = false;
		}
			
		/* we're done if found correct range */
		if (found_range){
			break;
		}
		char_found = goto_next_char(&str_ind, pw_formula, '{');
		if (!char_found){
			archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: could not find char %c\n", '{');
		}
	}
	/* the string index should never actually get to the end of the string because we should have found the range to which the 
	   current wire number corresponds */
	if (str_ind == str_size-1){
		archfpga_throw( __FILE__, __LINE__, "parse_piecewise_formula: could not find a closing '}'?\n");
	}

	/* at this point str_ind should point to '}' right before the formula we're interested in starts */
	/* get the value corresponding to this formula */
	tmp_ind_start = str_ind + 1;
	goto_next_char(&str_ind, pw_formula, ';');
	tmp_ind_count = str_ind - tmp_ind_start;			/* formula is between } and ; */
	substr = pw_formula.substr(tmp_ind_start, tmp_ind_count);

	/* now parse the formula corresponding to the appropriate piece-wise range */
	result = parse_formula(substr.c_str(), mydata);

	return result;
}


/* increments str_ind until it reaches specified char in formula. returns true if character was found, false otherwise */
static bool goto_next_char( int *str_ind, const string &pw_formula, char ch){
	bool result = true;
	int str_size = pw_formula.size();	
	if ((*str_ind) == str_size-1){
		archfpga_throw( __FILE__, __LINE__, "goto_next_char: passed-in str_ind is already at the end of string\n");
	}

	do{
		(*str_ind)++;
		if ( pw_formula[*str_ind] == ch ){
			/* found the next requested character */
			break;
		}

	} while ((*str_ind) != str_size-1);
	if ((*str_ind) == str_size-1 && pw_formula[*str_ind] != ch){
		result = false;
	}
	return result;
}


/* Parses the specified formula using a shunting yard algorithm (see wikipedia). The function's result 
   is stored in the rpn_output vector in reverse-polish notation */
static void formula_to_rpn( const char* formula, const t_formula_data &mydata, 
				vector<Formula_Object> &rpn_output ){

	stack<Formula_Object> op_stack;		/* stack for handling operators and brackets in formula */
	Formula_Object fobj;		 	/* for parsing formula objects */

	int ichar = 0;
	const char *ch = NULL;
	/* go through formula and build rpn_output along with op_stack until \0 character is hit */
	while(1) {
		ch = &formula[ichar];

		if ('\0' == (*ch)){
			/* we're done */
			break;
		} else if (' ' == (*ch)){
			/* skip space */
		} else {
			/* parse the character */
			get_formula_object( ch, ichar, mydata, &fobj );
			switch (fobj.type){
				case E_FML_NUMBER:
					/* add to output vector */
					rpn_output.push_back( fobj );
					break;
				case E_FML_OPERATOR:
					/* operators may be pushed to op_stack or rpn_output */
					handle_operator( fobj, rpn_output, op_stack);
					break;
				case E_FML_BRACKET:
					/* brackets are only ever pushed to op_stack, not rpn_output */
					handle_bracket( fobj, rpn_output, op_stack);
					break;
				default:
					archfpga_throw( __FILE__, __LINE__, "in formula_to_rpn: unknown formula object type: %d\n", fobj.type);
					break;
			}
		}
		ichar++;
	}

	/* pop all remaining operators off of stack */
	Formula_Object fobj_dummy;
	while ( !op_stack.empty() ){
		fobj_dummy = op_stack.top();

		if (E_FML_BRACKET == fobj_dummy.type){
			archfpga_throw( __FILE__, __LINE__, "in formula_to_rpn: Mismatched brackets in user-provided formula\n");
		}		

		rpn_output.push_back( fobj_dummy );
		op_stack.pop();
	}

	return;
}


/* Fills the formula object fobj according to specified character and mydata, 
   which help determine which numeric value, if any, gets assigned to fobj
   ichar is incremented by the corresponding count if the need to step through the 
   character array arises */
static void get_formula_object( const char *ch, int &ichar, const t_formula_data &mydata,
				 Formula_Object *fobj ){

	/* the character can either be part of a number, or it can be an object like W, t, (, +, etc
	   here we have to account for both possibilities */

	if ( is_char_number(*ch) ){
		/* we have a number -- use atoi to convert */
		stringstream ss;
		while ( is_char_number(*ch) ){
			ss << (*ch);
			ichar++;
			ch++;	
		}
		ichar --;
		fobj->type = E_FML_NUMBER;
		fobj->data.num = vtr::atoi(ss.str().c_str());
	} else {
		switch ((*ch)){
			case 'W':
				fobj->type = E_FML_NUMBER;
				fobj->data.num = mydata.dest_W;
				break;
			case 't':
				fobj->type = E_FML_NUMBER;
				fobj->data.num = mydata.wire;
				break;
			case '+':
				fobj->type = E_FML_OPERATOR;
				fobj->data.op = E_OP_ADD;
				break;
			case '-':
				fobj->type = E_FML_OPERATOR;
				fobj->data.op = E_OP_SUB;
				break;
			case '/':
				fobj->type = E_FML_OPERATOR;
				fobj->data.op = E_OP_DIV;
				break;
			case '*':
				fobj->type = E_FML_OPERATOR;
				fobj->data.op = E_OP_MULT;
				break;
			case '(':
				fobj->type = E_FML_BRACKET;
				fobj->data.left_bracket = true;
				break;
			case ')':
				fobj->type = E_FML_BRACKET;
				fobj->data.left_bracket = false;
				break;
			default:
				archfpga_throw( __FILE__, __LINE__, "in get_formula_object: unsupported character: %c\n", *ch);
				break; 
		}	
	}
	
	return;
}


/* returns integer specifying precedence of passed-in operator. higher integer 
   means higher precedence */
static int get_fobj_precedence( const Formula_Object &fobj ){
	int precedence = 0;

	if (E_FML_BRACKET == fobj.type){
		precedence = 0;
	} else if (E_FML_OPERATOR == fobj.type){
		t_operator op = fobj.data.op;
		switch (op){
			case E_OP_ADD: 
				precedence = 2;
				break;
			case E_OP_SUB: 
				precedence = 2;
				break;
			case E_OP_MULT: 
				precedence = 3;
				break;
			case E_OP_DIV: 
				precedence = 3;
				break;
			default:
				archfpga_throw( __FILE__, __LINE__, "in get_fobj_precedence: unrecognized operator: %d\n", op);
				break; 
		}
	} else {
		archfpga_throw( __FILE__, __LINE__, "in get_fobj_precedence: no precedence possible for formula object type %d\n", fobj.type);
	}
	
	return precedence;
}


/* Returns associativity of the specified operator */
static bool op_associativity_is_left( const t_operator &/*op*/ ){
	bool is_left = true;
	
	/* associativity is 'left' for all but the power operator, which is not yet implemented */
	//TODO:
	//if op is 'power' set associativity is_left=false and return

	return is_left;
}


/* used by the shunting-yard formula parser to deal with operators such as add and subtract */
static void handle_operator( const Formula_Object &fobj, vector<Formula_Object> &rpn_output,
				stack<Formula_Object> &op_stack){
	if ( E_FML_OPERATOR != fobj.type){
		archfpga_throw( __FILE__, __LINE__, "in handle_operator: passed in formula object not of type operator\n");
	}

	int op_pr = get_fobj_precedence( fobj );
	bool op_assoc_is_left = op_associativity_is_left( fobj.data.op );

	Formula_Object fobj_dummy;
	bool keep_going = false;
	do{
		/* here we keep popping operators off the stack onto back of rpn_output while
		   associativity of operator is 'left' and precedence op_pr = top_pr, or while
		   precedence op_pr < top_pr */

		/* determine whether we should keep popping operators off the op stack */
		if ( op_stack.empty() ){
			keep_going = false;
		} else {
			/* get precedence of top operator */
			int top_pr = get_fobj_precedence ( op_stack.top() );

			keep_going = ( (op_assoc_is_left && op_pr==top_pr)
					|| op_pr<top_pr );
			
			if (keep_going){
				/* pop top operator off stack onto the back of rpn_output */
				fobj_dummy = op_stack.top();
				rpn_output.push_back( fobj_dummy );
				op_stack.pop();
			}
		}

	} while (keep_going);

	/* place new operator object on top of stack */
	op_stack.push(fobj);
	
	return;
}


/* used by the shunting-yard formula parser to deal with brackets, ie '(' and ')' */
static void handle_bracket( const Formula_Object &fobj, vector<Formula_Object> &rpn_output,
				stack<Formula_Object> &op_stack ){
	if ( E_FML_BRACKET != fobj.type){
		archfpga_throw( __FILE__, __LINE__, "in handle_bracket: passed-in formula object not of type bracket\n");
	}

	/* check if left or right bracket */
	if ( fobj.data.left_bracket ){
		/* left bracket, so simply push it onto operator stack */
		op_stack.push(fobj);
	} else {
		bool keep_going = false;
		do{
			/* here we keep popping operators off op_stack onto back of rpn_output until a 
			   left bracket is encountered */

			if ( op_stack.empty() ){
				/* didn't find an opening bracket - mismatched brackets */
				archfpga_throw( __FILE__, __LINE__, "Ran out of stack while parsing brackets -- bracket mismatch in user-specified formula\n");
				keep_going = false;
			}
	
			Formula_Object next_fobj = op_stack.top();
			if (E_FML_BRACKET == next_fobj.type){
				if (next_fobj.data.left_bracket){
					/* matching bracket found -- pop off stack and finish */
					op_stack.pop();
					keep_going = false;
				} else {
					/* should not find two right brackets without a left bracket in-between */
					archfpga_throw( __FILE__, __LINE__, "Mismatched brackets encountered in user-specified formula\n");
					keep_going = false;
				}
			} else if (E_FML_OPERATOR == next_fobj.type){
				/* pop operator off stack onto the back of rpn_output */
				Formula_Object fobj_dummy = op_stack.top();
				rpn_output.push_back( fobj_dummy );
				op_stack.pop();
				keep_going = true;
			} else {
				archfpga_throw( __FILE__, __LINE__, "Found unexpected formula object on operator stack: %d\n", next_fobj.type);
				keep_going = false;
			}
		} while (keep_going);
	}
	return;
}


/* parses a reverse-polish notation vector corresponding to a switchblock formula
   and returns the integer result */
static int parse_rpn_vector( vector<Formula_Object> &rpn_vec ){
	int result = -1;

	/* first entry should always be a number */
	if (E_FML_NUMBER != rpn_vec[0].type){
		archfpga_throw( __FILE__, __LINE__, "parse_rpn_vector: first entry is not a number\n");
	}

	if (rpn_vec.size() == 1){
		/* if the vector size is 1 then we just have a number (which was verified above) */
		result = rpn_vec[0].data.num;
	} else {
		/* have numbers and operators */
		Formula_Object fobj;
		int ivec = 0;
		/* keep going until we have gone through the whole vector */
		while ( !rpn_vec.empty() ){
			
			/* keep going until we have hit an operator */
			do{
				ivec++;		/* first item should never be operator anyway */
				if (ivec == (int)rpn_vec.size()){
					archfpga_throw( __FILE__, __LINE__, "parse_rpn_vector(): found multiple numbers in switchblock formula, but no operator\n");
				}
			} while ( E_FML_OPERATOR != rpn_vec[ivec].type );

			/* now we apply the selected operation to the two previous entries */
			/* the result is stored in the object that used to be the operation */
			rpn_vec[ivec].data.num = apply_rpn_op( rpn_vec[ivec-2], rpn_vec[ivec-1], rpn_vec[ivec] );
			rpn_vec[ivec].type = E_FML_NUMBER;

			/* remove the previous two entries from the vector */
			rpn_vec.erase(rpn_vec.begin() + ivec - 2, rpn_vec.begin() + ivec - 0);
			ivec -= 2;

			/* if we're down to one element, we are done */
			if (1 == rpn_vec.size()){
				result = rpn_vec[ivec].data.num;
				rpn_vec.erase(rpn_vec.begin() + ivec);
			}
		}
	}

	return result;
}


/* applies operation specified by 'op' to the given arguments. arg1 comes before arg2 */
static int apply_rpn_op( const Formula_Object &arg1, const Formula_Object &arg2, 
					const Formula_Object &op ){
	int result = -1;
	
	/* arguments must be numbers */
	if ( E_FML_NUMBER != arg1.type || 
	     E_FML_NUMBER != arg2.type){
		archfpga_throw( __FILE__, __LINE__, "in apply_rpn_op: one of the arguments is not a number\n");
	}

	/* check that op is actually an operation */
	if ( E_FML_OPERATOR != op.type ){
		archfpga_throw( __FILE__, __LINE__, "in apply_rpn_op: the object specified as the operation is not of operation type\n");
	}

	/* apply operation to arguments */
	switch (op.data.op){
		case E_OP_ADD:
			result = arg1.data.num + arg2.data.num;
			break;
		case E_OP_SUB:
			result = arg1.data.num - arg2.data.num;
			break;
		case E_OP_MULT:
			result = arg1.data.num * arg2.data.num;
			break;
		case E_OP_DIV:
			result = arg1.data.num / arg2.data.num;
			break;
		default:
			archfpga_throw( __FILE__, __LINE__, "in apply_rpn_op: invalid operation: %d\n", op.data.op);
			break;
	}
	
	return result;
} 


/* checks if specified character represents an ASCII number */
static bool is_char_number ( const char ch ){
	bool result = false;
	
	if ( ch >= '0' && ch <= '9' ){
		result = true;
	} else {
		result = false;
	}

	return result;
}


/* checks if the specified formula is piece-wise defined */
static bool is_piecewise_formula( const char *formula ){
	bool result = false;
	/* if formula is piecewise, we expect '{' to be the very first character */
	if ('{' == formula[0]){
		result = true;
	} else {
		result = false;
	}
	return result;
}

