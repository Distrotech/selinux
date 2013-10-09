/* Workaround for http://bugs.python.org/issue4835 */
#ifndef SIZEOF_SOCKET_T
#define SIZEOF_SOCKET_T SIZEOF_INT
#endif

#include <Python.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sepol/sepol.h>
#include <sepol/policydb.h>
#include <sepol/policydb/services.h>
#include <selinux/selinux.h>

#define UNKNOWN -1
#define BADSCON -2
#define BADTCON -3
#define BADTCLASS -4
#define BADPERM -5
#define BADCOMPUTE -6
#define NOPOLICY -7
#define ALLOW 0
#define DONTAUDIT 1
#define TERULE 2
#define BOOLEAN 3
#define CONSTRAINT 4
#define RBAC 5

struct boolean_t {
	char *name;
	int active;
};

static struct boolean_t **boollist = NULL;
static int boolcnt = 0;

struct avc_t {
	sepol_handle_t *handle;
	sepol_policydb_t *policydb;
	sepol_security_id_t ssid;
	sepol_security_id_t tsid;
	sepol_security_class_t tclass;
	sepol_access_vector_t av;
};

static struct avc_t *avc = NULL;

static sidtab_t sidtab;

static int load_booleans(const sepol_bool_t * boolean,
			 void *arg __attribute__ ((__unused__)))
{
	boollist[boolcnt] = malloc(sizeof(struct boolean_t));
	boollist[boolcnt]->name = strdup(sepol_bool_get_name(boolean));
	boollist[boolcnt]->active = sepol_bool_get_value(boolean);
	boolcnt++;
	return 0;
}

static int check_booleans(struct boolean_t **bools)
{
	char errormsg[PATH_MAX];
	struct sepol_av_decision avd;
	unsigned int reason;
	int rc;
	int i;
	sepol_bool_key_t *key = NULL;
	sepol_bool_t *boolean = NULL;
	int fcnt = 0;
	int *foundlist = calloc(boolcnt, sizeof(int));
	if (!foundlist) {
		PyErr_SetString( PyExc_MemoryError, "Out of memory\n");
		return fcnt;
	}
	for (i = 0; i < boolcnt; i++) {
		char *name = boollist[i]->name;
		int active = boollist[i]->active;
		rc = sepol_bool_key_create(avc->handle, name, &key);
		if (rc < 0) {
			PyErr_SetString( PyExc_RuntimeError, 
					 "Could not create boolean key.\n");
			break;
		}
		rc = sepol_bool_query(avc->handle,
				      avc->policydb,
				      key, &boolean);

		if (rc < 0) {
			snprintf(errormsg, sizeof(errormsg), 
				 "Could not find boolean %s.\n", name);
			PyErr_SetString( PyExc_RuntimeError, errormsg);
			break;
		}

		sepol_bool_set_value(boolean, !active);

		rc = sepol_bool_set(avc->handle,
				    avc->policydb,
				    key, boolean);
		if (rc < 0) {
			snprintf(errormsg, sizeof(errormsg), 
				 "Could not set boolean data %s.\n", name);
			PyErr_SetString( PyExc_RuntimeError, errormsg);
			break;
		}

		/* Reproduce the computation. */
		rc = sepol_compute_av_reason(avc->ssid, avc->tsid, avc->tclass,
					     avc->av, &avd, &reason);
		if (rc < 0) {
			snprintf(errormsg, sizeof(errormsg), 
				 "Error during access vector computation, skipping...");
			PyErr_SetString( PyExc_RuntimeError, errormsg);

			sepol_bool_free(boolean);
			break;
		} else {
			if (!reason) {
				foundlist[fcnt] = i;
				fcnt++;
			}
			sepol_bool_set_value(boolean, active);
			rc = sepol_bool_set(avc->handle,
					    avc->policydb, key,
					    boolean);
			if (rc < 0) {
				snprintf(errormsg, sizeof(errormsg), 
					 "Could not set boolean data %s.\n",
					 name);
			
				PyErr_SetString( PyExc_RuntimeError, errormsg);
				break;
			}
		}
		sepol_bool_free(boolean);
		sepol_bool_key_free(key);
		key = NULL;
		boolean = NULL;
	}
	if (key)
		sepol_bool_key_free(key);

	if (boolean)
		sepol_bool_free(boolean);

	if (fcnt > 0) {
		*bools = calloc(sizeof(struct boolean_t), fcnt + 1);
		struct boolean_t *b = *bools;
		for (i = 0; i < fcnt; i++) {
			int ctr = foundlist[i];
			b[i].name = strdup(boollist[ctr]->name);
			b[i].active = !boollist[ctr]->active;
		}
	}
	free(foundlist);
	return fcnt;
}

static PyObject *finish(PyObject *self __attribute__((unused)), PyObject *args) {
	PyObject *result = 0;
  
	if (PyArg_ParseTuple(args,(char *)":finish")) {
		int i = 0;
		if (! avc)
			Py_RETURN_NONE;

		for (i = 0; i < boolcnt; i++) {
			free(boollist[i]->name);
			free(boollist[i]);
		}
		free(boollist);
		sepol_sidtab_shutdown(&sidtab);
		sepol_sidtab_destroy(&sidtab);
		sepol_policydb_free(avc->policydb);
		sepol_handle_destroy(avc->handle);
		free(avc);
		avc = NULL;
		boollist = NULL;
		boolcnt = 0;

		/* Boilerplate to return "None" */
		Py_RETURN_NONE;
	}
	return result;
}


static int __policy_init(const char *init_path)
{
	FILE *fp;
	char path[PATH_MAX];
	char errormsg[PATH_MAX];
	struct sepol_policy_file *pf = NULL;
	int rc;
	unsigned int cnt;

	path[PATH_MAX-1] = '\0';
	if (init_path) {
		strncpy(path, init_path, PATH_MAX-1);
		fp = fopen(path, "r");
		if (!fp) {
			snprintf(errormsg, sizeof(errormsg), 
				 "unable to open %s:  %s\n",
				 path, strerror(errno));
			PyErr_SetString( PyExc_ValueError, errormsg);
			return 1;
		}
	} else {
		fp = fopen(selinux_current_policy_path(), "r");
		if (!fp) {
			snprintf(errormsg, sizeof(errormsg), 
				 "unable to open %s:  %s\n",
				 selinux_current_policy_path(),
				 strerror(errno));
			PyErr_SetString( PyExc_ValueError, errormsg);
			return 1;
		}
	}

	avc = calloc(sizeof(struct avc_t), 1);
	if (!avc) {
		PyErr_SetString( PyExc_MemoryError, "Out of memory\n");
		fclose(fp);
		return 1;
	}

	/* Set up a policydb directly so that we can mutate it later
	   for testing what booleans might have allowed the access.
	   Otherwise, we'd just use sepol_set_policydb_from_file() here. */
	if (sepol_policy_file_create(&pf) ||
	    sepol_policydb_create(&avc->policydb)) {
		snprintf(errormsg, sizeof(errormsg), 
			 "policydb_init failed: %s\n", strerror(errno));
		PyErr_SetString( PyExc_RuntimeError, errormsg);
		fclose(fp);
		return 1;
	}
	sepol_policy_file_set_fp(pf, fp);	
	if (sepol_policydb_read(avc->policydb, pf)) {
		snprintf(errormsg, sizeof(errormsg), 
			 "invalid binary policy %s\n", path);
		PyErr_SetString( PyExc_ValueError, errormsg);
		fclose(fp);
		return 1;
	}
	fclose(fp);
	sepol_set_policydb(&avc->policydb->p);
	avc->handle = sepol_handle_create();
	/* Turn off messages */
	sepol_msg_set_callback(avc->handle, NULL, NULL);

	rc = sepol_bool_count(avc->handle,
			      avc->policydb, &cnt);
	if (rc < 0) {
		PyErr_SetString( PyExc_RuntimeError, "unable to get bool count\n");
		return 1;
	}

	boollist = calloc(cnt, sizeof(*boollist));
	if (!boollist) {
		PyErr_SetString( PyExc_MemoryError, "Out of memory\n");
		return 1;
	}

	sepol_bool_iterate(avc->handle, avc->policydb,
			   load_booleans, (void *)NULL);

	/* Initialize the sidtab for subsequent use by sepol_context_to_sid
	   and sepol_compute_av_reason. */
	rc = sepol_sidtab_init(&sidtab);
	if (rc < 0) {
		PyErr_SetString( PyExc_RuntimeError, "unable to init sidtab\n");
		free(boollist);
		return 1;
	}
	sepol_set_sidtab(&sidtab);
	return 0;
}

static PyObject *init(PyObject *self __attribute__((unused)), PyObject *args) {
  int result;
  char *init_path=NULL;
  if (avc) {
	  PyErr_SetString( PyExc_RuntimeError, "init called multiple times");
	  return NULL;
  }
  if (!PyArg_ParseTuple(args,(char *)"|s:policy_init",&init_path))
    return NULL;
  result = __policy_init(init_path);
  return Py_BuildValue("i", result);
}

#define RETURN(X) \
	PyTuple_SetItem(result, 0, Py_BuildValue("i", X));	\
	return result;						

static PyObject *analyze(PyObject *self __attribute__((unused)) , PyObject *args) {
	char *reason_buf = NULL;
	security_context_t scon; 
	security_context_t tcon;
	char *tclassstr; 
	PyObject *listObj;
	PyObject *strObj;
	int numlines;
	struct boolean_t *bools;
	unsigned int reason;
	sepol_security_id_t ssid, tsid;
	sepol_security_class_t tclass;
	sepol_access_vector_t perm, av;
	struct sepol_av_decision avd;
	int rc;
	int i=0;
	PyObject *result = PyTuple_New(2);
	if (!result) return NULL;
	Py_INCREF(Py_None);
	PyTuple_SetItem(result, 1, Py_None);

	if (!PyArg_ParseTuple(args,(char *)"sssO!:audit2why",&scon,&tcon,&tclassstr,&PyList_Type, &listObj)) 
		return NULL;
  
	/* get the number of lines passed to us */
	numlines = PyList_Size(listObj);

	/* should raise an error here. */
	if (numlines < 0)	return NULL; /* Not a list */

	if (!avc) {
		RETURN(NOPOLICY)
	}

	rc = sepol_context_to_sid(scon, strlen(scon) + 1, &ssid);
	if (rc < 0) {
		RETURN(BADSCON)
	}
	rc = sepol_context_to_sid(tcon, strlen(tcon) + 1, &tsid);
	if (rc < 0) {
		RETURN(BADTCON)
	}
	tclass = string_to_security_class(tclassstr);
	if (!tclass) {
		RETURN(BADTCLASS)
	}
	/* Convert the permission list to an AV. */
	av = 0;

	/* iterate over items of the list, grabbing strings, and parsing
	   for numbers */
	for (i=0; i<numlines; i++){
		char *permstr;

		/* grab the string object from the next element of the list */
		strObj = PyList_GetItem(listObj, i); /* Can't fail */
		
		/* make it a string */
#if PY_MAJOR_VERSION >= 3
		permstr = _PyUnicode_AsString( strObj );
#else
		permstr = PyString_AsString( strObj );
#endif
		
		perm = string_to_av_perm(tclass, permstr);
		if (!perm) {
			RETURN(BADPERM)
		}
		av |= perm;
	}

	/* Reproduce the computation. */
	rc = sepol_compute_av_reason_buffer(ssid, tsid, tclass, av, &avd, &reason, &reason_buf, 0);
	if (rc < 0) {
		RETURN(BADCOMPUTE)
	}

	if (!reason) {
		RETURN(ALLOW)
	}
	if (reason & SEPOL_COMPUTEAV_TE) {
		avc->ssid = ssid;
		avc->tsid = tsid;
		avc->tclass = tclass;
		avc->av = av;
		if (check_booleans(&bools) == 0) {
			if (av & ~avd.auditdeny) {
				RETURN(DONTAUDIT)
			} else {
				RETURN(TERULE)
			}
		} else {
			PyTuple_SetItem(result, 0, Py_BuildValue("i", BOOLEAN));
			struct boolean_t *b = bools;
			int len=0;
			while (b->name) {
				len++; b++;
			}
			b = bools;
			PyObject *outboollist = PyTuple_New(len);
			len=0;
			while(b->name) {
				PyObject *bool = Py_BuildValue("(si)", b->name, b->active);
				PyTuple_SetItem(outboollist, len++, bool);
				b++;
			}
			free(bools);
			PyTuple_SetItem(result, 1, outboollist);
			return result;
		}
	}

	if (reason & SEPOL_COMPUTEAV_CONS) {
		if (reason_buf) {
			PyObject *result = NULL;
			result = Py_BuildValue("is", CONSTRAINT, reason_buf);
			free(reason_buf);
			return result;
		}
		RETURN(CONSTRAINT);
	}

	if (reason & SEPOL_COMPUTEAV_RBAC)
		RETURN(RBAC)

        RETURN(BADCOMPUTE)
}

static PyMethodDef audit2whyMethods[] = {
    {"init",  init, METH_VARARGS,
     "Initialize policy database."},
    {"analyze",  analyze, METH_VARARGS,
     "Analyze AVC."},
    {"finish",  finish, METH_VARARGS,
     "Finish using policy, free memory."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#if PY_MAJOR_VERSION >= 3
/* Module-initialization logic specific to Python 3 */
struct module_state {
	/* empty for now */
};
static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"audit2why",
	NULL,
	sizeof(struct module_state),
	audit2whyMethods,
	NULL,
	NULL,
	NULL,
	NULL
};

PyMODINIT_FUNC PyInit_audit2why(void); /* silence -Wmissing-prototypes */
PyMODINIT_FUNC PyInit_audit2why(void)
#else
PyMODINIT_FUNC initaudit2why(void); /* silence -Wmissing-prototypes */
PyMODINIT_FUNC initaudit2why(void)
#endif
{
	PyObject *m;
#if PY_MAJOR_VERSION >= 3
	m = PyModule_Create(&moduledef);
	if (m == NULL) {
		return NULL;
	}
#else
	m  = Py_InitModule("audit2why", audit2whyMethods);
#endif
	PyModule_AddIntConstant(m,"UNKNOWN", UNKNOWN);
	PyModule_AddIntConstant(m,"BADSCON", BADSCON);
	PyModule_AddIntConstant(m,"BADTCON", BADTCON);
	PyModule_AddIntConstant(m,"BADTCLASS", BADTCLASS);
	PyModule_AddIntConstant(m,"BADPERM", BADPERM);
	PyModule_AddIntConstant(m,"BADCOMPUTE", BADCOMPUTE);
	PyModule_AddIntConstant(m,"NOPOLICY", NOPOLICY);
	PyModule_AddIntConstant(m,"ALLOW", ALLOW);
	PyModule_AddIntConstant(m,"DONTAUDIT", DONTAUDIT);
	PyModule_AddIntConstant(m,"TERULE", TERULE);
	PyModule_AddIntConstant(m,"BOOLEAN", BOOLEAN);
	PyModule_AddIntConstant(m,"CONSTRAINT", CONSTRAINT);
	PyModule_AddIntConstant(m,"RBAC", RBAC);

#if PY_MAJOR_VERSION >= 3
	return m;
#endif
}
