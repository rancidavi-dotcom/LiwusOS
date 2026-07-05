# Plano de Arquitetura: VFS e Hierarquia de Pastas do LiwusOS

## 1. Visão Geral
Transformar o sistema de arquivos atual em um VFS (Virtual File System) unificado, permitindo montagem de diferentes dispositivos em caminhos específicos.

## 2. Estrutura de Diretórios Proposta
- `/` : Raiz virtual (Kernel VFS).
- `/bin/` ou `/initrd/` : Ponto de montagem do **initrd** (Somente Leitura). Contém os binários básicos do sistema (lua, liw, tcc, etc).
- `/house/localhost/` : Ponto de montagem do **Disco FAT32** (Leitura/Escrita). Este é o "Home" do usuário.
- `/dev/` : Dispositivos de hardware (teclado, mouse, vga).

## 3. Regras de Acesso
- **Initrd (`/`)**: Ao dar `ls /`, o sistema deve mostrar os arquivos vitais. Tentativas de escrita aqui devem retornar erro (Read-Only).
- **Home (`/house/localhost/`)**: Local para criação de scripts, códigos C e arquivos pessoais. Persistente no `liwus_disk.img`.

## 4. Implementação Técnica
1.  **VFS Manager:** Criar uma estrutura no Kernel que gerencie "Mount Points".
2.  **Path Resolution:** A syscall `open()` deve quebrar o caminho (ex: `/house/localhost/oi.c`) e decidir qual driver chamar (FAT32 ou Initrd).
3.  **Terminal Update:** Atualizar o comando `cd` e `ls` para navegar nessa árvore unificada.

## 5. Próximos Passos
- [ ] Refatorar `src/fs/vfs.c` para suportar montagem de nós.
- [ ] Mapear o driver FAT32 para o prefixo `/house/localhost/`.
- [ ] Garantir que o `initrd` seja montado como a base `/`.
