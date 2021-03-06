#include <api.h>

void FilterStr_one_term() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {{ TagA }}
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_one_term_w_inout() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA, .inout = EcsIn }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "[in] TagA");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_two_terms() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA },
            { TagB }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA, TagB");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_two_terms_w_inout() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA },
            { TagB, .inout = EcsIn }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA, [in] TagB");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_three_terms_w_or() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);
    ECS_TAG(world, TagC);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA },
            { TagB, .oper = EcsOr },
            { TagC, .oper = EcsOr }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA, TagB || TagC");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_three_terms_w_or_inout() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);
    ECS_TAG(world, TagC);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA },
            { TagB, .oper = EcsOr, .inout = EcsIn },
            { TagC, .oper = EcsOr, .inout = EcsIn }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA, [in] TagB || TagC");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_four_terms_three_w_or_inout() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, TagA);
    ECS_TAG(world, TagB);
    ECS_TAG(world, TagC);
    ECS_TAG(world, TagD);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { TagA },
            { TagB, .oper = EcsOr, .inout = EcsIn },
            { TagC, .oper = EcsOr, .inout = EcsIn },
            { TagD, .inout = EcsIn }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "TagA, [in] TagB || TagC, [in] TagD");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_switch() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Sw);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { ECS_SWITCH | Sw }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "SWITCH|Sw");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_case() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Cs);
    ECS_TYPE(world, Sw, Cs);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { ecs_case(Sw, Cs) }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "CASE|(Sw,Cs)");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_switch_from_entity() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Sw);
    ECS_TAG(world, e);
    

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { ECS_SWITCH | Sw, .subj.entity = e }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "SWITCH|Sw(e)");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}

void FilterStr_case_from_entity() {
    ecs_world_t *world = ecs_mini();

    ECS_TAG(world, Sw);
    ECS_TAG(world, Cs);
    ECS_TAG(world, e);

    ecs_filter_t f;
    test_int(0, ecs_filter_init(world, &f, &(ecs_filter_desc_t) {
        .terms = {
            { ecs_case(Sw, Cs), .subj.entity = e }
        }
    }));

    char *str = ecs_filter_str(world, &f);
    test_str(str, "CASE|Sw(e,Cs)");
    ecs_os_free(str);

    ecs_filter_fini(&f);

    ecs_fini(world);
}
