#include <gtk/gtk.h>
#include <pthread.h>
#include <libappindicator/app-indicator.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_FILE "dockermonitor.cfg"

const char* cmd_docker_ps = "docker ps -a --format '{\"ID\":\"{{.ID}}\",\"Name\":\"{{.Names}}\",\"State\":\"{{.State}}\",\"Status\":\"{{.Status}}\"}'";
const char* cmd_docker_stop = "docker stop";
const char* cmd_docker_start = "docker start";

typedef struct {
    char *id;
    char *name;
    char *state;
    char *status;
    char is_running;
    void *next;
} t_container;

typedef struct {
    t_container *first;
    t_container *last;
    int count;
} t_container_list;

char *to_watch = NULL;
t_container_list container_list;

static GtkWidget *about_window = NULL;
static GtkWidget *docker_status_window = NULL;
static GtkWidget *config_window = NULL;
static GtkWidget *config_text_view = NULL;
static GtkListStore *list_store = NULL;
char *config_path = NULL;

void get_docker_containers();
char *get_config_path();

void container_set_prop(char **p, const char* val) {
    *p = (char*)malloc(strlen(val) * sizeof(char*));
    strcpy(*p, val);
}

void add_container(const char *id, const char *name, const char *state, const char *status) {
    t_container *tc = (t_container*)malloc(sizeof(t_container));
    container_set_prop(&(tc->id), id);
    container_set_prop(&(tc->name), name);
    container_set_prop(&(tc->state), state);
    container_set_prop(&(tc->status), status);
    tc->next = NULL;
    if(container_list.first == NULL) {
        container_list.first = container_list.last = tc;
    } else {
        container_list.last->next = tc;
        container_list.last = tc;
    }
    container_list.count++;
}

int remove_container_all() {
    if(container_list.first == NULL) return 0;
    int count = 0;
    t_container *tc = container_list.first, *tn = NULL;
    while(tc != NULL) {
        tn = tc->next;
        free(tc->id);
        free(tc->name);
        free(tc->state);
        free(tc->status);
        free(tc);
        tc = tn;
        count++;
    }
    container_list.first = NULL;
    container_list.last = NULL;
    return count;
}

char remove_container(const char *id) {
    int i = 0;
    for(t_container *tc = container_list.first, *last = NULL; 
        i < container_list.count; 
        last = tc, tc = tc->next, i++) {
        if(strcmp(tc->id, id) == 0) {
            if(container_list.first == tc) {
                container_list.first = tc->next;
            } else if (container_list.last == tc) {
                last->next = tc->next;
                container_list.last = last;
            } else {
                last->next = tc->next;
            }
            free(tc->id);
            free(tc->name);
            free(tc->state);
            free(tc->status);
            free(tc);
            container_list.count--;
            return 1;
        }
    }
    return 0;
}

t_container *exists_container(const char *id) {
    if(container_list.first == NULL) return NULL;
    int i = 0;
    for(t_container *tc = container_list.first; 
        i < container_list.count; 
        tc = tc->next, i++) {
        if(strcmp(tc->id, id) == 0)
            return tc;
    }
    return NULL;
}

char container_is_running(t_container *c) {
    if(c == NULL) return 0;
    c->is_running = (char)(strcmp(c->state, "running") == 0 ? 1 : 0);
    return c->is_running;
}

char in_watchlist(const char *key) {
    if(to_watch == NULL) return 0;
    return (char)(strstr(to_watch, key) != NULL ? 1 : 0);
}

char *get_config_path() {
    if(config_path != NULL) return config_path;
    const char *home = (char*)g_get_home_dir(); const char *cfg = "/.config/";
    int cp_len = (strlen(home) + strlen(cfg) + strlen(CONFIG_FILE)) + 1;
    config_path = (char*) malloc(cp_len + sizeof(char));
    sprintf(config_path, "%s%s%s", home, cfg, CONFIG_FILE);
    return config_path;
}

void load_configuration() {
    FILE *file = fopen(get_config_path(), "r");
    if (!file) return;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);

    fseek(file, 0, SEEK_SET);

    to_watch = (char *)malloc(length + 1);
    fread(to_watch, 1, length, file);
    to_watch[length] = '\0';

    fclose(file);
}

void save_configuration(GtkWidget *widget, gpointer data) {
    if (!config_text_view) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(config_text_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    if(to_watch != NULL) 
        free(to_watch);

    ulong length = strlen(text);
    to_watch = (char *)malloc(length + 1);
    strcpy(to_watch, text);
    to_watch[length] = '\0';

    FILE *file = fopen(get_config_path(), "w");
    if (file) {
        fwrite(text, sizeof(char), strlen(text), file);
        fclose(file);
    }

    // get_docker_containers();

    g_free(text);
    gtk_window_close(widget);
}

void show_configuration_window(GtkWidget *widget, gpointer data) {
    if (!config_window) {
        config_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(config_window), "Configuration");
        gtk_window_set_default_size(GTK_WINDOW(config_window), 400, 300);
        g_signal_connect(config_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &config_window);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        gtk_container_add(GTK_CONTAINER(config_window), vbox);

        config_text_view = gtk_text_view_new();
        gtk_box_pack_start(GTK_BOX(vbox), config_text_view, TRUE, TRUE, 0);

        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

        GtkWidget *save_button = gtk_button_new_with_label("Save");
        g_signal_connect(save_button, "clicked", G_CALLBACK(save_configuration), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), save_button, TRUE, TRUE, 0);

        GtkWidget *close_button = gtk_button_new_with_label("Close");
        g_signal_connect(close_button, "clicked", G_CALLBACK(gtk_window_close), config_window);
        gtk_box_pack_start(GTK_BOX(hbox), close_button, TRUE, TRUE, 0);
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(config_text_view));
    gtk_text_buffer_set_text(buffer, to_watch, -1);

    gtk_widget_show_all(config_window);
}

void show_about_window(GtkWidget *widget, gpointer data) {
    if (!about_window) {
        about_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(about_window), "About");
        gtk_window_set_default_size(GTK_WINDOW(about_window), 300, 150);
        g_signal_connect(about_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &about_window);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        gtk_container_add(GTK_CONTAINER(about_window), vbox);

        GtkWidget *label = gtk_label_new("This is a simple application to control docker containers");
        gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);

        GtkWidget *ok_button = gtk_button_new_with_label("OK");
        gtk_box_pack_start(GTK_BOX(vbox), ok_button, FALSE, TRUE, 0);
        g_signal_connect(ok_button, "clicked", G_CALLBACK(gtk_window_close), about_window);
    }

    gtk_widget_show_all(about_window);
}

void get_docker_containers() {
    remove_container_all();
    t_container *tc = NULL;
    FILE *fp = popen(cmd_docker_ps, "r");
    if (fp) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), fp)) {
            struct json_object *parsed_json = json_tokener_parse(buffer);
            struct json_object *id, *name, *state, *status;

            json_object_object_get_ex(parsed_json, "ID", &id);
            json_object_object_get_ex(parsed_json, "Name", &name);
            json_object_object_get_ex(parsed_json, "State", &state);
            json_object_object_get_ex(parsed_json, "Status", &status);

            const char *t_id = json_object_get_string(id);
            const char *t_name = json_object_get_string(name);
            const char *t_state = json_object_get_string(state);
            const char *t_status = json_object_get_string(status);

            if(!in_watchlist(t_id) && !in_watchlist(t_name)) {
                json_object_put(parsed_json);
                continue;
            }

            add_container(t_id, t_name, t_state, t_status);

            json_object_put(parsed_json);
        }
        pclose(fp);
    }
}

void on_checkbox_toggled(GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data) {
    GtkListStore *store = GTK_LIST_STORE(user_data);
    GtkTreeIter iter; char active;

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_str)) {
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 4, &active, -1);
        active = !active;
        gtk_list_store_set(store, &iter, 4, active, -1);
    }
}

void apply_docker_commands() {
    char active; char *cid; char *cname; char command[50];
    gboolean has_next; GtkTreeIter iter; t_container *tc;

    for(
        has_next = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list_store), &iter); 
        has_next != 0; 
        free(cid), free(cname), has_next = gtk_tree_model_iter_next(GTK_TREE_MODEL(list_store), &iter)
        ) {
        gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, 
                0, &cid, 
                1, &cname,
                4, &active,
        -1);
        tc = exists_container(cid);
        if(tc == NULL || (tc->is_running == active)) {
            continue;
        }
        sprintf(command, "%s %s", active ? cmd_docker_start : cmd_docker_stop, cid);
        FILE *fp = popen(command, "r");
        if (fp) pclose(fp);
    }
    
    // if(list_store) {
    //     has_next = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list_store), &iter);
    //     while(has_next) {
    //         gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, 
    //             0, &cid, 
    //             1, &cname,
    //             4, &active,
    //         -1);
    //         tc = exists_container(cid);
    //         if(tc == NULL || (tc->is_running == active)) {
    //             has_next = gtk_tree_model_iter_next(GTK_TREE_MODEL(list_store), &iter);    
    //             continue;
    //         }
    //         sprintf(command, "%s %s", active ? cmd_docker_start : cmd_docker_stop, cid);
    //         FILE *fp = popen(command, "r");
    //         if (fp) pclose(fp);
    //         free(cid); free(cname);
    //         has_next = gtk_tree_model_iter_next(GTK_TREE_MODEL(list_store), &iter);
    //     }
    // }
    if (docker_status_window) {
        gtk_window_close(docker_status_window);
    }
}

void show_docker_status_window(GtkWidget *widget, gpointer data) {
    if (!docker_status_window) {
        docker_status_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(docker_status_window), "Docker Containers Status");
        gtk_window_set_default_size(GTK_WINDOW(docker_status_window), 600, 400);
        g_signal_connect(docker_status_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &docker_status_window);


        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        gtk_container_add(GTK_CONTAINER(docker_status_window), vbox);


        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        // gtk_container_add(GTK_CONTAINER(docker_status_window), scrolled_window);
        gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);


        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);


        list_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_CHAR);
        
        GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
        gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

        GtkCellRenderer *renderer_text = gtk_cell_renderer_text_new();
        
        GtkCellRenderer *renderer_toggle = gtk_cell_renderer_toggle_new();
        g_signal_connect(renderer_toggle, "toggled", G_CALLBACK(on_checkbox_toggled), list_store);

        GtkTreeViewColumn *col_id = gtk_tree_view_column_new_with_attributes("Container ID", renderer_text, "text", 0, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_id);

        GtkTreeViewColumn *col_name = gtk_tree_view_column_new_with_attributes("Name", renderer_text, "text", 1, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

        GtkTreeViewColumn *col_state = gtk_tree_view_column_new_with_attributes("State", renderer_text, "text", 2, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_state);

        GtkTreeViewColumn *col_status = gtk_tree_view_column_new_with_attributes("Status", renderer_text, "text", 3, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_status);

        GtkTreeViewColumn *col_checkbox = gtk_tree_view_column_new_with_attributes("Check", renderer_toggle, "active", 4, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_checkbox);

        




        GtkWidget *ok_button = gtk_button_new_with_label("Ok");
        g_signal_connect(ok_button, "clicked", G_CALLBACK(apply_docker_commands), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), ok_button, TRUE, TRUE, 0);

        GtkWidget *close_button = gtk_button_new_with_label("Close");
        g_signal_connect(close_button, "clicked", G_CALLBACK(gtk_window_close), docker_status_window);
        gtk_box_pack_start(GTK_BOX(hbox), close_button, TRUE, TRUE, 0);

    }

    get_docker_containers();
    gtk_list_store_clear(list_store);
    t_container *tc = container_list.first;
    while(tc != NULL) {
        GtkTreeIter iter;
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter, 
            0, tc->id, 
            1, tc->name, 
            2, tc->state, 
            3, tc->status, 
            4, container_is_running(tc),
            -1);
        tc = tc->next;
    }

    gtk_widget_show_all(docker_status_window);
}

void call_quit_application(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    
    load_configuration();
    // get_docker_containers();

    AppIndicator *indicator = app_indicator_new(
        "dockermonitor-tray-icon", 
        "face-cool", 
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *about_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_item, "activate", G_CALLBACK(show_about_window), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about_item);

    GtkWidget *docker_status_item = gtk_menu_item_new_with_label("Docker Status");
    g_signal_connect(docker_status_item, "activate", G_CALLBACK(show_docker_status_window), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), docker_status_item);

    GtkWidget *config_item = gtk_menu_item_new_with_label("Configuration");
    g_signal_connect(config_item, "activate", G_CALLBACK(show_configuration_window), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), config_item);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(call_quit_application), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);

    app_indicator_set_menu(indicator, GTK_MENU(menu));

    gtk_main();

    return 0;
}

