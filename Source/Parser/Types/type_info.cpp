#include "Parser/Types/type_info.hpp"

namespace walnut {
namespace parser_types {

void TemplateArgument::write_to(std::ostream& os) const {
    if (is_type()) {
        type.write_to(os);
    } else {
        os << (value_repr.empty() ? "<expr>" : value_repr);
    }

    if (is_pack) os << "...";
}

void TypeInfo::write_to(std::ostream& os) const {
    if (has_modifiers()) {
        modifiers.write_to(os);
        os << ' ';
    }

    if (type) {
        type->write_to(os);
    } else {
        os << "<no type>";
    }

    if (has_template_args()) {
        os << '<';

        for (std::size_t i = 0; i < template_args.size(); ++i) {
            if (i > 0) os << ", ";
            template_args[i]->write_to(os);
        }

        os << '>';
    }

    if (has_indirection()) { indirection.write_to(os); }
}

TypeInfo TypeInfo::clone_into(Arena& arena) const {
    TypeInfo copy;
    copy.type = type ? type->clone_into(arena) : nullptr;
    copy.modifiers = modifiers;
    copy.indirection = indirection;
    copy.resolved = resolved;
    copy.has_angle_args = has_angle_args;
    copy.template_args.reserve(template_args.size());

    for (const TemplateArgument* a : template_args) {
        TemplateArgument* na = make_in<TemplateArgument>(arena);
        na->form       = a->form;
        na->type       = a->type.clone_into(arena);
        na->value      = a->value;       
        na->value_repr = a->value_repr;
        na->is_pack    = a->is_pack;
        copy.template_args.push_back(na);
    }

    return copy;
}

} // namespace parser_types
} // namespace walnut
