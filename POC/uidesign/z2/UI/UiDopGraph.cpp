#include <z2/UI/UiDopGraph.h>
#include <z2/UI/UiDopNode.h>
#include <z2/UI/UiDopScene.h>
#include <z2/UI/UiDopEditor.h>
#include <z2/dop/DopTable.h>


namespace z2::UI {


void UiDopGraph::select_child(GraphicsWidget *ptr, bool multiselect) {
    GraphicsView::select_child(ptr, multiselect);
    if (editor)
        editor->set_selection(dynamic_cast<UiDopNode *>(ptr));
}


bool UiDopGraph::remove_link(UiDopLink *link) {
    if (remove_child(link)) {
        link->from_socket->links.erase(link);
        link->to_socket->links.erase(link);
        auto to_node = link->to_socket->get_parent();
        auto from_node = link->from_socket->get_parent();
        bk_graph->remove_node_input(to_node->bk_node,
                link->to_socket->get_index());
        links.erase(link);
        return true;
    } else {
        return false;
    }
}


bool UiDopGraph::remove_node(UiDopNode *node) {
    bk_graph->remove_node(node->bk_node);
    for (auto *socket: node->inputs) {
        for (auto *link: std::set(socket->links)) {
            remove_link(link);
        }
    }
    for (auto *socket: node->outputs) {
        for (auto *link: std::set(socket->links)) {
            remove_link(link);
        }
    }
    if (remove_child(node)) {
        nodes.erase(node);
        return true;
    } else {
        return false;
    }
}


UiDopNode *UiDopGraph::add_node(std::string kind) {
    auto p = add_child<UiDopNode>();
    p->bk_node = bk_graph->add_node(kind);
    p->name = p->bk_node->name;
    p->kind = p->bk_node->kind;
    nodes.insert(p);
    return p;
}


UiDopLink *UiDopGraph::add_link(UiDopOutputSocket *from_socket, UiDopInputSocket *to_socket) {
    auto p = add_child<UiDopLink>(from_socket, to_socket);
    auto to_node = to_socket->get_parent();
    auto from_node = from_socket->get_parent();
    bk_graph->set_node_input(to_node->bk_node, to_socket->get_index(),
            from_node->bk_node, from_socket->get_index());
    links.insert(p);
    return p;
}


// add a new pending link with one side linked to |socket| if no pending link
// create a real link from current pending link's socket to the |socket| otherwise
void UiDopGraph::add_pending_link(UiDopSocket *socket) {
    if (pending_link) {
        if (socket && pending_link->socket) {
            auto socket1 = pending_link->socket;
            auto socket2 = socket;
            auto output1 = dynamic_cast<UiDopOutputSocket *>(socket1);
            auto output2 = dynamic_cast<UiDopOutputSocket *>(socket2);
            auto input1 = dynamic_cast<UiDopInputSocket *>(socket1);
            auto input2 = dynamic_cast<UiDopInputSocket *>(socket2);
            if (output1 && input2) {
                add_link(output1, input2);
            } else if (input1 && output2) {
                add_link(output2, input1);
            }
        } else if (auto another = dynamic_cast<UiDopInputSocket *>(pending_link->socket); another) {
            another->clear_links();
        }
        remove_child(pending_link);
        pending_link = nullptr;

    } else if (socket) {
        pending_link = add_child<UiDopPendingLink>(socket);
    }
}


UiDopGraph::UiDopGraph() {
    auto n1 = add_node("readobj", {400, 384});
    auto n2 = add_node("route", {100, 128});
    auto n3 = add_node("first", {700, 256});
    n1->bk_node->inputs[0].value = "assets/monkey.obj";
    add_link(n1->outputs[0], n3->inputs[0]);
    add_link(n2->outputs[0], n3->inputs[1]);

    auto btn = add_child<Button>();
    btn->text = "Apply";
    btn->on_clicked.connect([this] () {
        std::string expr = "@first1:lhs";
        dop::DopDepsgraph deps;
        bk_graph->resolve_depends(expr, &deps);
        deps.execute();
        auto val = bk_graph->resolve_value(expr);
        get_parent()->set_view_result(val);
    });
}


void UiDopGraph::paint() const {
    glColor3f(0.2f, 0.2f, 0.2f);
    glRectf(bbox.x0, bbox.y0, bbox.x0 + bbox.nx, bbox.y0 + bbox.ny);
}


void UiDopGraph::on_event(Event_Mouse e) {
    GraphicsView::on_event(e);

    if (e.down != true)
        return;

    if (e.btn == 1) {
        if (pending_link) {
            remove_child(pending_link);
            pending_link = nullptr;
        } else {
            select_child(nullptr, false);
        }
    }

    if (e.btn != 0)
        return;

    auto item = item_at({cur.x, cur.y}, [=] (Widget *it) {
        // filter away UiDopPendingLink and UiDopLink:
        return !dynamic_cast<GraphicsLineItem *>(it);
    });

    if (auto node = dynamic_cast<UiDopNode *>(item); node) {
        if (pending_link) {
            auto another = pending_link->socket;
            if (dynamic_cast<UiDopInputSocket *>(another) && node->outputs.size()) {
                add_pending_link(node->outputs[0]);
            } else if (dynamic_cast<UiDopOutputSocket *>(another) && node->inputs.size()) {
                add_pending_link(node->inputs[0]);
            } else {
                add_pending_link(nullptr);
            }
        }

#if 0
    } else if (auto link = dynamic_cast<UiDopLink *>(item); link) {
        if (pending_link) {
            auto another = pending_link->socket;
            if (dynamic_cast<UiDopInputSocket *>(another)) {
                add_pending_link(link->from_socket);
            } else if (dynamic_cast<UiDopOutputSocket *>(another)) {
                add_pending_link(link->to_socket);
            } else {
                add_pending_link(nullptr);
            }
        }
#endif

    } else if (auto socket = dynamic_cast<UiDopSocket *>(item); socket) {
        add_pending_link(socket);

    } else {
        add_pending_link(nullptr);
    }
}


UiDopNode *UiDopGraph::add_node(std::string kind, Point pos) {
    auto node = add_node(kind);
    node->position = pos;
    node->kind = kind;
    auto const &desc = dop::tab.desc_of(kind);
    for (auto const &sock_info: desc.inputs) {
        auto socket = node->add_input_socket();
        socket->name = sock_info.name;
    }
    for (auto const &sock_info: desc.outputs) {
        auto socket = node->add_output_socket();
        socket->name = sock_info.name;
    }
    node->update_sockets();
    return node;
}


UiDopContextMenu *UiDopGraph::add_context_menu() {
    remove_context_menu();

    menu = get_parent()->add_child<UiDopContextMenu>();
    menu->position = position + translate + Point(cur.x, cur.y) * scaling;
    for (auto const &key: dop::tab.entry_names()) {
        menu->add_entry(key);
    }
    menu->update_entries();

    menu->on_selected.connect([this] {
        add_node(menu->selection, (menu->position - position - translate) * (1.f / scaling));
        remove_context_menu();
    });

    return menu;
}


void UiDopGraph::remove_context_menu() {
    if (menu) {
        get_parent()->remove_child(menu);
        menu = nullptr;
    }
}


void UiDopGraph::on_event(Event_Key e) {
    Widget::on_event(e);

    if (e.down != true)
        return;

    if (e.key == GLFW_KEY_TAB) {
        if (!menu)
            add_context_menu();
        else
            remove_context_menu();

    } else if (e.key == GLFW_KEY_DELETE) {
        for (auto *item: children_selected) {
            if (auto link = dynamic_cast<UiDopLink *>(item); link) {
                remove_link(link);
            } else if (auto node = dynamic_cast<UiDopNode *>(item); node) {
                remove_node(node);
            }
        }
        children_selected.clear();
        select_child(nullptr, false);
    }
}


}  // namespace z2::UI
