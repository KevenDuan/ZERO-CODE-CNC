# ZERO-CODE-CNC

An intelligent desktop application that automatically converts CAD drawings into CNC machining instructions without requiring any G-code programming knowledge.

## Overview

This software bridges the gap between CAD design and CNC machining by automatically analyzing DXF drawings and generating ready-to-use machining instructions. Users simply import their CAD files and the system intelligently recognizes geometric features, suggests optimal machining strategies, and outputs complete toolpaths - no manual programming required.

## Key Features

### Intelligent Feature Recognition
- **Automatic Geometry Analysis**: Intelligently identifies machining features from CAD drawings
- **Smart Feature Classification**: 
  - Outer contours for roughing/finishing operations
  - Circular holes for drilling cycles
  - Inner pockets for cavity machining
  - Islands for material retention
  - Open profiles for contour cutting
- **Layer-Based Processing**: Respects CAD layer organization for operation sequencing

### No-Programming Workflow
- **One-Click Import**: Simply load DXF files - no manual geometry reconstruction
- **Automatic Toolpath Generation**: Creates optimal cutting strategies without user intervention
- **Visual Verification**: Interactive preview of machining operations before execution
- **Collision-Free Paths**: Intelligent path planning prevents tool collisions

### Professional CNC Output
- **Multi-Format Support**: Compatible with various CNC controllers
- **Optimized Cutting Parameters**: Automatically calculates feeds, speeds, and depths
- **Tool Change Management**: Organizes operations for minimal tool changes
- **Safety Considerations**: Includes approach/retract strategies and safety heights

## Technical Architecture

### Core Processing Engine
- **Python-Based Analysis**: Advanced geometric algorithms for feature recognition
- **Tolerance Management**: Configurable precision settings for different machining requirements
- **Chain Building Intelligence**: Connects disconnected segments into continuous toolpaths
- **Nested Feature Handling**: Correctly processes complex geometries with multiple levels

### User Interface
- **Qt6 Modern Interface**: Professional-grade desktop application
- **Visual CAD Viewer**: Interactive display of imported drawings
- **Feature Table**: Detailed listing of recognized machining operations
- **Real-Time Feedback**: Processing status and validation messages

### Processing Pipeline
1. **DXF Import**: Reads CAD drawings with comprehensive entity support
2. **Geometry Extraction**: Converts drawing entities to machining geometries
3. **Feature Recognition**: Identifies holes, pockets, contours, and profiles
4. **Strategy Assignment**: Applies appropriate machining strategies to each feature
5. **Toolpath Generation**: Creates optimized cutting paths
6. **Output Generation**: Produces ready-to-use CNC programs

## Supported CAD Entities

### Basic Geometry
- **Lines**: Straight cutting paths and boundaries
- **Arcs**: Curved contours and rounded features
- **Circles**: Drilling operations and circular pockets
- **Polylines**: Complex contours and profiles

### Advanced Geometry
- **Splines**: Smooth curves with automatic sampling
- **LWPolylines**: Lightweight polyline optimization
- **Polyline Compositions**: Multi-segment continuous paths

## Installation

### System Requirements
- Windows 10/11 (64-bit)
- 4GB RAM minimum (8GB recommended)
- 500MB available disk space
- OpenGL 3.3 compatible graphics

### Quick Setup
```bash
# Clone repository
git clone https://github.com/KevenDuan/ZERO-CODE-CNC.git
cd ZERO-CODE-CNC

# Install Python dependencies
pip install ezdxf

# Build application
mkdir build && cd build
cmake ..
cmake --build .
```

### Dependencies
- **Python 3.7+**: For geometry processing engine
- **Qt6**: For modern user interface
- **ezdxf**: For DXF file parsing
- **CMake**: For build management

## Usage Guide

### Basic Workflow
1. **Import CAD File**: Click import and select your DXF drawing
2. **Review Features**: Check automatically recognized machining operations
3. **Generate Toolpaths**: Click process to create cutting strategies
4. **Export Program**: Save generated CNC program for your machine

### Advanced Options
- **Tolerance Settings**: Adjust precision for different machining requirements
- **Layer Filtering**: Select specific CAD layers for processing
- **Feature Editing**: Modify automatically recognized features if needed
- **Custom Strategies**: Override default machining parameters

### Output Formats
- **Standard G-Code**: Compatible with most CNC controllers
- **Feature Report**: Detailed analysis of recognized geometries
- **SVG Preview**: Visual representation of machining operations

## Configuration Options

### Processing Parameters
- **Tolerance**: Point deduplication precision (default: 0.05mm)
- **Connection Tolerance**: Segment joining distance (default: 0.2mm)
- **Arc Resolution**: Curve sampling density (default: 24 segments)

### Machining Strategies
- **Roughing Passes**: Automatic material removal calculation
- **Finishing Allowance**: Stock remaining for final passes
- **Safety Heights**: Clearance planes for safe tool movement
- **Feed Rate Optimization**: Automatic speed calculation based on material

## Professional Features

### Quality Assurance
- **Geometry Validation**: Checks for invalid or problematic geometries
- **Collision Detection**: Prevents tool interference with workpiece
- **Path Optimization**: Minimizes air cutting and tool changes
- **Error Reporting**: Detailed diagnostics for problematic features

### Productivity Tools
- **Batch Processing**: Handle multiple drawings simultaneously
- **Template System**: Save and reuse processing configurations
- **Operation Sequencing**: Intelligent ordering for efficiency
- **Post-Processor Support**: Adaptable output for different machines

## Technical Specifications

### Geometry Processing
- **Point Deduplication**: Removes redundant points for smoother paths
- **Segment Connection**: Intelligently joins disconnected entities
- **Circular Detection**: Identifies perfect circles for drilling cycles
- **Containment Analysis**: Determines feature relationships (holes in pockets, etc.)

### Feature Classification
- **Outer Contours**: Primary workpiece boundaries
- **Inner Features**: Cavities, holes, and internal geometries
- **Circular Features**: Optimized for drilling operations
- **Open Profiles**: Contour cutting paths

## Troubleshooting

### Common Issues
- **Import Failures**: Check DXF version compatibility
- **Feature Recognition**: Adjust tolerance settings for complex drawings
- **Path Quality**: Modify sampling parameters for smooth curves
- **Performance**: Reduce complexity for very large drawings

### Optimization Tips
- **Layer Organization**: Use CAD layers to control processing order
- **Simplify Geometry**: Remove unnecessary detail before importing
- **Proper Scaling**: Ensure correct units in CAD drawing
- **Clean Geometry**: Avoid overlapping or duplicate entities

## Support and Development

### Professional Support
This software is designed for professional CNC machining environments, providing reliable automation for common programming tasks while maintaining the flexibility needed for complex manufacturing requirements.

### Future Enhancements
- **3D Feature Recognition**: Support for 3D CAD models
- **Adaptive Machining**: Dynamic parameter adjustment based on conditions
- **Machine Integration**: Direct communication with CNC controllers
- **Cloud Processing**: Distributed computation for complex geometries

## License

Professional-grade CNC automation software designed for manufacturing environments requiring reliable, no-programming solutions for CAD-to-CNC workflows.

---

**Transform your CAD drawings into CNC programs without writing a single line of G-code!**