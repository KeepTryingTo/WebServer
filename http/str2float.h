#include <iostream>
#include <string>
#include <cctype>
#include <cstdlib>

class Str2Float
{
public:
    Str2Float() {}
    ~Str2Float() {}
    float robust_stof(const char *text, float default_value = 0.5f)
    {
        if (text == nullptr)
        {
            std::cerr << "错误：空指针" << std::endl;
            return default_value;
        }

        // 检查是否为空字符串
        if (text[0] == '\0')
        {
            std::cerr << "错误：空字符串" << std::endl;
            return default_value;
        }

        // 检查是否只包含数字、小数点、正负号和科学计数法符号
        bool has_digit = false;
        bool has_dot = false;
        bool has_e = false;

        for (int i = 0; text[i] != '\0'; ++i)
        {
            char c = text[i];

            if (std::isdigit(c))
            {
                has_digit = true;
            }
            else if (c == '.' && !has_dot)
            {
                has_dot = true;
            }
            else if ((c == 'e' || c == 'E') && !has_e)
            {
                has_e = true;
            }
            else if ((c == '+' || c == '-') && (i == 0 || text[i - 1] == 'e' || text[i - 1] == 'E'))
            {
                // 正负号只能在开头或e/E后面
                continue;
            }
            else if (std::isspace(c))
            {
                std::cerr << "错误：字符串包含空白字符: \"" << text << "\"" << std::endl;
                return default_value;
            }
            else
            {
                std::cerr << "错误：无效字符 '" << c << "' 在字符串: \"" << text << "\"" << std::endl;
                return default_value;
            }
        }

        if (!has_digit)
        {
            std::cerr << "错误：没有数字字符: \"" << text << "\"" << std::endl;
            return default_value;
        }

        try
        {
            return std::stof(text);
        }
        catch (...)
        {
            std::cerr << "未知转换错误: \"" << text << "\"" << std::endl;
            return default_value;
        }
    }
};
