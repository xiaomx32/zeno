'''
Unified APIs
'''

from . import cpp, py, npy


def dumpDescriptors():
    return py.dumpDescriptors() + cpp.dumpDescriptors()

def addNode(type, name):
    if py.isPyNodeType(type):
        py.addNode(type, name)
    else:
        cpp.addNode(type, name)

def initNode(name):
    if py.isPyNodeName(name):
        py.initNode(name)
    else:
        cpp.initNode(name)

def applyNode(name):
    if py.isPyNodeName(name):
        py.applyNode(name)
    else:
        deps = cpp.getNodeRequirements(name)
        for dep in deps:
            requireObject(dep, is_py_dst=False)
        cpp.applyNode(name)

def cpp2pyObject(name):
    obj = npy.getCppObject(name)
    py.setObject(name, obj)

def py2cppObject(name):
    obj = py.getObject(name)
    npy.setCppObject(name, obj)

def setNodeInput(name, key, srcname):
    if py.isPyNodeName(name):
        py.setNodeInput(name, key, srcname)
    else:
        cpp.setNodeInput(name, key, srcname)

def setNodeParam(name, key, value):
    if py.isPyNodeName(name):
        py.setNodeParam(name, key, value)
    else:
        cpp.setNodeParam(name, key, value)


visited = set()
is_py2cpp_table = set()
is_cpp2py_table = set()

def invalidateAllObjects():
    visited.clear()

def requireObject(srcname, is_py_dst=True):
    if srcname in visited:
        return
    visited.add(srcname)

    nodename, sockname = srcname.split('::')
    applyNode(nodename)

    is_py_src = py.isPyNodeName(nodename)

    if not is_py_dst:
        if srcname in is_py2cpp_table or is_py_src:
            #print('py2cpp', srcname)
            is_py2cpp_table.add(srcname)
            py2cppObject(srcname)
    else:
        if srcname in is_cpp2py_table or not is_py_src:
            #print('cpp2py', srcname)
            is_cpp2py_table.add(srcname)
            cpp2pyObject(srcname)


__all__ = [
    'addNode',
    'initNode',
    'applyNode',
    'setNodeInput',
    'setNodeParam',
    'dumpDescriptors',
]
