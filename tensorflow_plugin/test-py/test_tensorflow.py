# -*- coding: iso-8859-1 -*-
# Maintainer: Andrew White

import hoomd, hoomd.md
hoomd.context.initialize()
import hoomd.tensorflow_plugin
import unittest
import os, tempfile, shutil
import numpy as np, math

#TODO: write test for changing particle number dynamically

class test_simple(unittest.TestCase):
    def test_ipc_to_tensor_import(self):
        import tensorflow as tf
        #TODO: pick a better path
        ipc_to_tensor_module = tf.load_op_library('/srv/hoomd-blue/build/hoomd/tensorflow_plugin/ipc2tensor/lib_ipc2tensor_op.so')
        ipc_to_tensor = ipc_to_tensor_module.ipc_to_tensor

class test_compute(unittest.TestCase):
    def test_compute_loop(self):
        hoomd.context.initialize()
        N = 3 * 3
        hoomd.init.create_lattice(unitcell=hoomd.lattice.sq(a=4.0),
                                           n=[3,3])
        nl_c = hoomd.md.nlist.cell(check_period=1)
        lj = hoomd.md.pair.lj(r_cut=2.5, nlist=nl_c)
        lj.pair_coeff.set('A', 'A', epsilon=1.0, sigma=1.0)
        hoomd.md.integrate.mode_standard(dt=0.005)
        hoomd.md.integrate.nvt(group=hoomd.group.all(), kT=1.2, tau=0.5)

        #build tf model
        #### RUN THIS ####
        # I can't run on this process because TF gets confused
        # if 2 instances are running. I don't understand it.
        # you can uncomment then kill when deadlock occurs
        #see test-cases/tf-restore for minimal example
        save_loc = '/tmp'
        '''
        import tensorflow as tf
        x = tf.Variable(tf.random_uniform([N, 4], name='nlist'))
        w = tf.Variable(tf.random_uniform([N, 4], name='positions'))
        y = tf.multiply(x, w)
        z = tf.reshape(y, [-1, 4], name='forces')

        with tf.Session() as sess:
            saver = tf.train.Saver()
            sess.run(tf.global_variables_initializer())
            saver.save(sess, os.path.join(save_loc, 'model'))
        '''
        ##### DONE WITH THIS CODE ####

        nlist = hoomd.md.nlist.tree()
        #only use nearest 1 neighbor (since nlist dimension is N x 4)
        tfcompute = hoomd.tensorflow_plugin.tfcompute.tensorflow(save_loc, nlist, nneighbor_cutoff=1)
        hoomd.run(5)


if __name__ == '__main__':
    unittest.main(argv = ['test_tensorflow.py', '-v'])
