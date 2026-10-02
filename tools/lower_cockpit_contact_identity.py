"""Independent approved lower-cockpit source/runtime and dependency identities."""

RUNTIME_SHA256 = '31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42'

POLICY_SHA256 = 'da32508e5b8b062ba622576d8c3ce23828b2f119235542ece1cc66ffa73d7020'

SOURCE_SHA256 = {'wayfarer-floor-evidence-01/checks.json': '917ff53094f7bb2c72159052fd1fc83203ba8ef64283e76c2b412c03fbddae0c',
 'wayfarer-floor-evidence-01/evidence.json': '3aae9c319521a5b41bdbd8a0ee2ab580a26c0ec287705c2205b065fd8898b28f',
 'wayfarer-floor-evidence-01/findings.json': 'fce2c06cfd1c32224e05a5e3d01074cef8ed502a39a5d3c688515f47e2b8e0f6',
 'wayfarer-floor-evidence-01/halo.json': '31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42',
 'wayfarer-floor-evidence-01/licenses/HOPPER_GENERATED_CONCEPTS.md': 'e0b30edddcc4e847687c3510fe29b83ecdaabaccc777b89164304ee69b0c5879',
 'wayfarer-floor-evidence-01/licenses/HOPPER_MESHY_TRIAL.md': '0b4d01c948f92f2d13b85c6f8f19c8dc77696285c8b764e5a3fc506446178dad',
 'wayfarer-floor-evidence-01/licenses/LICENSE.md': '984659cb7e96b257c5190f461f9ef2313971f28dc84be4495d926bd1196b3a6d',
 'wayfarer-floor-evidence-01/licenses/MESHY_QUALIFICATION_OUTPUT.md': '46456e7d2ee3ba826b67cbd4c4d9611755ca1d35a1776afa4257a927fa014225',
 'wayfarer-floor-evidence-01/licenses/STATION_KIT_01.md': 'db66c2c3dca629867586d156c3956a45de85e6a45e657df951fe4d2bd9c354b7',
 'wayfarer-floor-evidence-01/provenance.json': 'df64d9357a8c0bfba7ba5ae9a88a47f0bfd81038fd95d262f11fcf5a17140aea',
 'tools/extract_boarding_floor_evidence.py': '17ee09bc9927ee91870e1b95585a0b84dbfdac641915098a1d389e7658a37e3b',
 'tools/check_boarding_floor_evidence.py': 'ec3be1b47277dbab224137e153bcef05bce36fd9b273bf4c58b47404c5d39fe6'}

ENGINE_BINDINGS = {'assets/native/wayfarer-operating-02/metadata/contact.json': '109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a',
 'tools/export_boarding_contact.py': '20353bd2f3ef5182a70339fb4a5ad1ae1c99874640aa38c0c95b0a6520f5201c',
 'tools/wayfarer_operating_blender.py': '00bd9327cea482518cf4bf5811f21a351b50309323d64a93c991d9356007e261',
 'tools/wayfarer_operating_spec.py': '41bc77dd225a059427525113da3cef46db3b794937f433af818d31f3bb2f3113',
 'tools/station_clearance_blender.py': 'ab21962c6ecbc0219caf09cd665e3dc0589045c75cd9e79622cf75412e995212',
 'assets/native/boarding-support-01/boarding-support-01.json': '58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3',
 'assets/native/boarding-support-01/sources/wayfarer-boarding-support-01/support.json': '2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2',
 'assets/native/operating-motion-01/operating-motion-01.json': 'afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298',
 'assets/native/wayfarer-operating-02/package.json': '9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957',
 'assets/native/wayfarer-operating-02/metadata/wayfarer-operating-02.json': 'db7b0a2de2516adf9925abe74133a6c523bd334062884a1bfc403448c2b1e72e',
 'assets/native/wayfarer-operating-02/metadata/provenance.json': 'efe100447b6ebd028b41ee326c66ab8c30f088f0ff7aa8c800ad531f22e4ecbf',
 'assets/native/wayfarer-operating-02/payloads/model-00.xz-part': '1f36dfe61c558ec3285ec862bcf262a1bf6d3a20065f4b90e9cece27ce518174'}

IDENTITIES = {'halo_sha256': '31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42',
 'evidence_sha256': '3aae9c319521a5b41bdbd8a0ee2ab580a26c0ec287705c2205b065fd8898b28f',
 'source_provenance_sha256': 'df64d9357a8c0bfba7ba5ae9a88a47f0bfd81038fd95d262f11fcf5a17140aea',
 'extractor_sha256': '17ee09bc9927ee91870e1b95585a0b84dbfdac641915098a1d389e7658a37e3b',
 'checker_sha256': 'ec3be1b47277dbab224137e153bcef05bce36fd9b273bf4c58b47404c5d39fe6',
 'contact_sha256': '109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a',
 'support_source_sha256': '2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2',
 'support_runtime_sha256': '58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3',
 'operating_motion_sha256': 'afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298',
 'craft_model_sha256': 'a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c',
 'craft_source_sha256': '87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677',
 'operating_package_sha256': '9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957'}

POLICY = {'schema': 'apsis.lower-cockpit-contact-policy/1',
 'version': 1,
 'halo_runtime': 'lower-cockpit-contact-01.json',
 'identities': {'halo_sha256': '31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42',
                'evidence_sha256': '3aae9c319521a5b41bdbd8a0ee2ab580a26c0ec287705c2205b065fd8898b28f',
                'source_provenance_sha256': 'df64d9357a8c0bfba7ba5ae9a88a47f0bfd81038fd95d262f11fcf5a17140aea',
                'extractor_sha256': '17ee09bc9927ee91870e1b95585a0b84dbfdac641915098a1d389e7658a37e3b',
                'checker_sha256': 'ec3be1b47277dbab224137e153bcef05bce36fd9b273bf4c58b47404c5d39fe6',
                'contact_sha256': '109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a',
                'support_source_sha256': '2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2',
                'support_runtime_sha256': '58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3',
                'operating_motion_sha256': 'afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298',
                'craft_model_sha256': 'a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c',
                'craft_source_sha256': '87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677',
                'operating_package_sha256': '9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957'},
 'source_dependencies_sha256': {'assets/native/wayfarer-operating-02/metadata/contact.json': '109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a',
                                'tools/export_boarding_contact.py': '20353bd2f3ef5182a70339fb4a5ad1ae1c99874640aa38c0c95b0a6520f5201c',
                                'tools/wayfarer_operating_blender.py': '00bd9327cea482518cf4bf5811f21a351b50309323d64a93c991d9356007e261',
                                'tools/wayfarer_operating_spec.py': '41bc77dd225a059427525113da3cef46db3b794937f433af818d31f3bb2f3113',
                                'tools/station_clearance_blender.py': 'ab21962c6ecbc0219caf09cd665e3dc0589045c75cd9e79622cf75412e995212',
                                'assets/native/boarding-support-01/boarding-support-01.json': '58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3',
                                'assets/native/boarding-support-01/sources/wayfarer-boarding-support-01/support.json': '2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2',
                                'assets/native/operating-motion-01/operating-motion-01.json': 'afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298',
                                'assets/native/wayfarer-operating-02/package.json': '9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957',
                                'assets/native/wayfarer-operating-02/metadata/wayfarer-operating-02.json': 'db7b0a2de2516adf9925abe74133a6c523bd334062884a1bfc403448c2b1e72e',
                                'assets/native/wayfarer-operating-02/metadata/provenance.json': 'efe100447b6ebd028b41ee326c66ab8c30f088f0ff7aa8c800ad531f22e4ecbf',
                                'assets/native/wayfarer-operating-02/payloads/model-00.xz-part': '1f36dfe61c558ec3285ec862bcf262a1bf6d3a20065f4b90e9cece27ce518174'},
 'coordinate_contract': 'Blender(x,y,z)->(x,z,-y); source-rest world baked',
 'quantization_metres': 1e-06,
 'coverage': {'operation': 'union',
              'original_static_bounds_micrometres': [[-1050000, -200000, -3500000],
                                                     [1050000, 2970000, 6350000]],
              'lower_static_bounds_micrometres': [[-1050000, -850000, -3500000],
                                                  [1050000, -200000, -500000]]},
 'namespaces': {'original': 'original_contact_group_triangle',
                'halo': 'halo_global_triangle',
                'source': 'evaluated_source_object_triangle'},
 'ownership': {'owner': 'craft', 'motion_group': None, 'default_collision': 'obstacle'},
 'transforms': {'source_corrections': 'already_baked_once',
                'static_geometry': 'craft_rest',
                'moving_geometry': 'existing_authoritative_group_delta_once'},
 'counts': {'vertices': 4598, 'triangles': 8100, 'objects': 75},
 'limits': ['Coverage is the declared static-domain union, not the enclosing box or retained '
            'triangle extrema.',
            'Moving obstacles retain existing full group ownership and authoritative motion.',
            'Source matrices are archived affine provenance and are never reapplied.',
            'Independent intake matched 21 ordered source faces; it did not replay all 8100 source '
            'triangles.',
            'No standing, stair gait, actor action, support acquisition, seat occupancy or save '
            'change is authorized.',
            'Attached station, broader exterior and mechanism sweep coverage remain unqualified.']}

HALO_HEADER = {'schema_version': 1,
 'id': 'wayfarer-cockpit-contact-halo-01',
 'status': 'Exact-source additive proposal; not admitted contact or support permission',
 'contact_sha256': '109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a',
 'sources': {'station_reference_sha256': '6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6',
             'wayfarer_sha256': '87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677',
             'station_closure_sha256': '6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4'},
 'coordinate_contract': 'Blender(x,y,z)->(x,z,-y); source-rest world baked',
 'quantization_metres': 1e-06,
 'query_bounds_rest_m': [[-1.05, -0.85, -3.5], [1.05, -0.2, -0.5]],
 'selection': 'Static source triangles intersecting this bounded box and excluded by the existing '
              'craft contact crop; complete triangles retained, no clipping/filler/remeshing.',
 'derivative_corrections_baked_once': {'schema_version': 1,
                                       'axes': 'Blender source local',
                                       'operations': [{'id': 'roof_hinge_link_clearance',
                                                       'reason': 'Move four existing internal '
                                                                 'links inward to clear the '
                                                                 'pressure ceiling through the '
                                                                 'authored hatch rotation.',
                                                       'source_objects': [{'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            'link '
                                                                                            '-1 '
                                                                                            '-0.2',
                                                                           'source_parent': 'AFT01 '
                                                                                            '| '
                                                                                            'dock '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            '-1',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.05500000715255737,
                                                                                                              -0.19999980926513672,
                                                                                                              -0.01900005340576172]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.07999998331069946,
                                                                                                               -0.19999980926513672,
                                                                                                               -0.01900005340576172]],
                                                                           'local_translation_metres': [0.02499999850988388,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            'link '
                                                                                            '-1 '
                                                                                            '0.2',
                                                                           'source_parent': 'AFT01 '
                                                                                            '| '
                                                                                            'dock '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            '-1',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.05500000715255737,
                                                                                                              0.19999980926513672,
                                                                                                              -0.01900005340576172]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.07999998331069946,
                                                                                                               0.19999980926513672,
                                                                                                               -0.01900005340576172]],
                                                                           'local_translation_metres': [0.02499999850988388,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            'link '
                                                                                            '1 '
                                                                                            '-0.2',
                                                                           'source_parent': 'AFT01 '
                                                                                            '| '
                                                                                            'dock '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            '1',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [-0.05500000715255737,
                                                                                                              -0.19999980926513672,
                                                                                                              -0.01900005340576172]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [-0.07999998331069946,
                                                                                                               -0.19999980926513672,
                                                                                                               -0.01900005340576172]],
                                                                           'local_translation_metres': [-0.02499999850988388,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            'link '
                                                                                            '1 0.2',
                                                                           'source_parent': 'AFT01 '
                                                                                            '| '
                                                                                            'dock '
                                                                                            'hatch '
                                                                                            'hinge '
                                                                                            '1',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [-0.05500000715255737,
                                                                                                              0.19999980926513672,
                                                                                                              -0.01900005340576172]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [-0.07999998331069946,
                                                                                                               0.19999980926513672,
                                                                                                               -0.01900005340576172]],
                                                                           'local_translation_metres': [-0.02499999850988388,
                                                                                                        0.0,
                                                                                                        0.0]}],
                                                       'parameters': {'inward_metres': 0.025}},
                                                      {'id': 'inner_door_stroke',
                                                       'reason': 'Limit the four authored leaf '
                                                                 'displacements to the verified '
                                                                 'useful aperture before inlays '
                                                                 'reach fixed pressure-frame '
                                                                 'members.',
                                                       'source_objects': [{'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'inner '
                                                                                            'sliding '
                                                                                            'leaf '
                                                                                            'rig '
                                                                                            '-1 0',
                                                                           'source_parent': None,
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              0.0]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               0.0]],
                                                                           'local_translation_metres': [0.0,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'inner '
                                                                                            'sliding '
                                                                                            'leaf '
                                                                                            'rig '
                                                                                            '-1 1',
                                                                           'source_parent': None,
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              0.0]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               0.0]],
                                                                           'local_translation_metres': [0.0,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'inner '
                                                                                            'sliding '
                                                                                            'leaf '
                                                                                            'rig 1 '
                                                                                            '0',
                                                                           'source_parent': None,
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              0.0]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               0.0]],
                                                                           'local_translation_metres': [0.0,
                                                                                                        0.0,
                                                                                                        0.0]},
                                                                          {'source_object': 'AFT01 '
                                                                                            '| '
                                                                                            'inner '
                                                                                            'sliding '
                                                                                            'leaf '
                                                                                            'rig 1 '
                                                                                            '1',
                                                                           'source_parent': None,
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              0.0]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               0.0]],
                                                                           'local_translation_metres': [0.0,
                                                                                                        0.0,
                                                                                                        0.0]}],
                                                       'parameters': {'authored_fraction': 0.55}},
                                                      {'id': 'seat_lock_withdrawal',
                                                       'reason': 'Withdraw the existing lock on '
                                                                 'its source local X axis before '
                                                                 'swivel rotation and restore only '
                                                                 'after endpoint alignment.',
                                                       'source_objects': [{'source_object': 'RIG | '
                                                                                            'positive '
                                                                                            'swivel '
                                                                                            'lock',
                                                                           'source_parent': 'RIG | '
                                                                                            'fore-aft '
                                                                                            'carriage',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.23499999940395355,
                                                                                                              0.0,
                                                                                                              0.23000000417232513]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.23499999940395355,
                                                                                                               0.0,
                                                                                                               0.23000000417232513]],
                                                                           'local_translation_metres': [0.0,
                                                                                                        0.0,
                                                                                                        0.0]}],
                                                       'parameters': {'withdrawal_metres': 0.07,
                                                                      'withdraw_begin': 0.5,
                                                                      'withdraw_end': 0.6,
                                                                      'turn_begin': 0.6,
                                                                      'turn_end': 0.95,
                                                                      'restore_begin': 0.95,
                                                                      'restore_end': 1.0}},
                                                      {'id': 'seat_lock_bolt_clearance',
                                                       'reason': 'Relocate the single existing '
                                                                 'lock-axis fastener around its '
                                                                 'unchanged mounting pitch circle; '
                                                                 'the source housing has no '
                                                                 'dedicated hole or boss.',
                                                       'source_objects': [{'source_object': 'Swivel '
                                                                                            'mounting '
                                                                                            'bolt',
                                                                           'source_parent': 'RIG | '
                                                                                            'fore-aft '
                                                                                            'carriage',
                                                                           'original_rest_local_transform': [[1.0,
                                                                                                              0.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              1.0,
                                                                                                              0.0],
                                                                                                             [0.0,
                                                                                                              0.0,
                                                                                                              1.0],
                                                                                                             [0.23999999463558197,
                                                                                                              0.0,
                                                                                                              0.23100000619888306]],
                                                                           'operating_rest_local_transform': [[1.0,
                                                                                                               0.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               1.0,
                                                                                                               0.0],
                                                                                                              [0.0,
                                                                                                               0.0,
                                                                                                               1.0],
                                                                                                              [0.2379467636346817,
                                                                                                               0.0313262939453125,
                                                                                                               0.23100000619888306]],
                                                                           'local_translation_metres': [-0.0020532310009002686,
                                                                                                        0.0313262864947319,
                                                                                                        0.0]}],
                                                       'parameters': {'pitch_circle_radius_metres': 0.23999999463558197,
                                                                      'angle_radians': 0.1308996938995747}}]},
 'motion_contract': 'Source-rest craft coordinates. Apply only the existing craft root/world '
                    'placement; this static halo has no extra local correction or moving group.',
 'policy': 'All proposed triangles are obstacles. No new boot/grasp permissions, missing floor '
           'replacement, posture/step acceptance or standing-policy relaxation.'}
