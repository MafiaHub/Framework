#version 330

in vec4 in_var_COLOR0;
layout(location = 0) out vec4 out_var_SV_Target;

void main()
{
    out_var_SV_Target = in_var_COLOR0;
}

