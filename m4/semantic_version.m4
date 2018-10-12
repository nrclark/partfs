#serial 1

m4_define([SEMANTIC_VERSION_FILE],[/dev/null])

m4_define([_newline],[
])

m4_define([_strip_trailing],[m4_bpatsubst($1, [[.][0-9]+$],[])])
m4_define([_strip_leading],[m4_bpatsubst($1, [^[0-9]+[.]],[])])

m4_define([SEMANTIC_VERSION],[0.0.0])
m4_define([SEMANTIC_VERSION_MAJOR],[0])
m4_define([SEMANTIC_VERSION_MINOR],[0])
m4_define([SEMANTIC_VERSION_PATCH],[0])

m4_define([SEMANTIC_VERSION_REQUIRE_FILE],
    [m4_esyscmd([test -e $1])
     m4_cond(m4_sysval, [0], [],
             [m4_errprint([Fatal error: file $1 not found.],_newline)
              m4_exit(1)]
            )
    ]
)

m4_define([SEMANTIC_VERSION_SET_FILE],
          [SEMANTIC_VERSION_REQUIRE_FILE([$1])
           m4_ifdef([SEMANTIC_VERSION], [m4_undefine([SEMANTIC_VERSION])])
           m4_ifdef([SEMANTIC_VERSION_MAJOR], [m4_undefine([SEMANTIC_VERSION_MAJOR])])
           m4_ifdef([SEMANTIC_VERSION_MINOR], [m4_undefine([SEMANTIC_VERSION_MINOR])])
           m4_ifdef([SEMANTIC_VERSION_PATCH], [m4_undefine([SEMANTIC_VERSION_PATCH])])
           m4_define([SEMANTIC_VERSION],[m4_esyscmd_s([cat $1])])
           m4_define([SEMANTIC_VERSION_MAJOR],[_strip_trailing([_strip_trailing([SEMANTIC_VERSION])])])
           m4_define([SEMANTIC_VERSION_MINOR],[_strip_leading([_strip_trailing([SEMANTIC_VERSION])])])
           m4_define([SEMANTIC_VERSION_PATCH],[_strip_leading([_strip_leading([SEMANTIC_VERSION])])])
          ])

m4_define([yoiks],[jeepers])
