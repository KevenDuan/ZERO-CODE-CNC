import sys
import ezdxf
from ezdxf.addons.drawing import RenderContext, Frontend
from ezdxf.addons.drawing.svg import SVGBackend
from ezdxf.addons.drawing.layout import Page  # 【新增】引入页面布局模块

def convert_dxf_to_svg(dxf_path, svg_path):
    try:
        # 1. 加载 DXF 文件
        doc = ezdxf.readfile(dxf_path)
        msp = doc.modelspace()
        
        # 2. 配置 SVG 渲染后端
        backend = SVGBackend()
        ctx = RenderContext(doc)
        frontend = Frontend(ctx, backend)
        
        # 3. 绘制图形
        frontend.draw_layout(msp, finalize=True)
        
        # 【核心修复】：告诉引擎自动计算图纸边界（宽 0，高 0 代表自适应）
        page = Page(0, 0)
        svg_string = backend.get_string(page)
        
        # 4. 暴力美学：强制把黑色/白色的线条替换成康睿经典的 CAD 红色
        svg_string = svg_string.replace('stroke="#000000"', 'stroke="#FF5252"')
        svg_string = svg_string.replace('stroke="#ffffff"', 'stroke="#FF5252"')
        svg_string = svg_string.replace('stroke="black"', 'stroke="#FF5252"')
        svg_string = svg_string.replace('stroke="white"', 'stroke="#FF5252"')
        
        # 5. 写入 SVG 文件
        with open(svg_path, "wt", encoding="utf-8") as fp:
            fp.write(svg_string)
            
        print("SUCCESS")
    except Exception as e:
        print(f"ERROR: {str(e)}")

if __name__ == "__main__":
    if len(sys.argv) == 3:
        convert_dxf_to_svg(sys.argv[1], sys.argv[2])