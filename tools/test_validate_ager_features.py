#!/usr/bin/env python3
"""Contraejemplos de aceptación regional: profundidad, detalles y cobertura."""
import unittest
import numpy as np
from validate_ager_features import regions


class RegionalGateTests(unittest.TestCase):
    def setUp(self):
        self.scene={'camera':{'position':[0,0,0],'target':[0,0,1],'up':[0,1,0],'fov':45},
                    'features':[{'name':'lamina','min':[-2,-2,.5],'max':[2,2,2]}]}
        self.exact=np.ones((2,2))

    def test_depth_error_in_both_directions(self):
        for candidate in (self.exact+.3,self.exact-.3):
            item=regions(self.exact,candidate,self.scene,.1)[0]
            self.assertFalse(item['regional_pass'])
            self.assertEqual(item['missing_pixels'],4)

    def test_small_feature_cannot_pass_when_missing(self):
        exact=np.full((2,2),np.inf);exact[0,0]=1
        item=regions(exact,np.full((2,2),np.inf),self.scene,.1)[0]
        self.assertTrue(item['visible'])
        self.assertTrue(item['completely_missing'])
        self.assertFalse(item['regional_pass'])

    def test_unobserved_is_pending(self):
        item=regions(np.full((2,2),np.inf),self.exact,self.scene,.1)[0]
        self.assertEqual(item['coverage_status'],'unobserved')
        self.assertIsNone(item['regional_pass'])

    def test_identical_depth_passes(self):
        self.assertTrue(regions(self.exact,self.exact,self.scene,.1)[0]['regional_pass'])


if __name__=='__main__':
    unittest.main()
